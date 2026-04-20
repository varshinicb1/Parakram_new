//! Graph composer — merges multiple templates into one unified node graph.
//!
//! Key rules:
//! - Nodes are deduplicated by `name` (first occurrence wins).
//! - Topics are inferred from node pub/sub declarations.
//! - Device-specific Parakram topics are injected based on device_ids.

use super::templates::GraphTemplate;
use super::{NodeDef, TopicDef};
use std::collections::HashMap;
use serde_json::json;

pub fn compose(
    templates: &[GraphTemplate],
    _prompt: &str,
    device_ids: &[String],
) -> (Vec<NodeDef>, Vec<TopicDef>) {
    let mut nodes: Vec<NodeDef> = Vec::new();
    let mut seen_names: std::collections::HashSet<String> = std::collections::HashSet::new();

    // Merge nodes from all selected templates, dedup by name
    for template in templates {
        for node in (template.nodes)() {
            if seen_names.insert(node.name.clone()) {
                nodes.push(node);
            }
        }
    }

    // Inject per-device Parakram bridge nodes
    for device_id in device_ids {
        let safe = device_id.replace('-', "_").to_lowercase();
        let bridge_name = format!("parakram_bridge_{safe}");
        if seen_names.insert(bridge_name.clone()) {
            nodes.push(NodeDef {
                package: "parakram_ros".into(),
                executable: "device_manager".into(),
                name: bridge_name,
                parameters: json!({
                    "device_filter": device_id,
                    "poll_rate_hz": 1.0,
                }),
                remappings: vec![],
                publishes: vec![
                    format!("/parakram/{safe}/telemetry"),
                    format!("/parakram/{safe}/status"),
                ],
                subscribes: vec!["/parakram/command".into()],
            });
        }
    }

    let topics = infer_topics(&nodes);
    (nodes, topics)
}

/// Build a topic list from node pub/sub declarations.
fn infer_topics(nodes: &[NodeDef]) -> Vec<TopicDef> {
    // publisher_name → (msg_type, hz)
    let mut pub_map: HashMap<String, (String, f32, String)> = HashMap::new();
    // topic → vec of subscriber node names
    let mut sub_map: HashMap<String, Vec<String>> = HashMap::new();

    for node in nodes {
        for topic in &node.publishes {
            let (msg_type, hz) = guess_type_hz(topic);
            pub_map.entry(topic.clone()).or_insert((msg_type, hz, node.name.clone()));
        }
        for topic in &node.subscribes {
            sub_map.entry(topic.clone()).or_default().push(node.name.clone());
        }
    }

    let mut all_topics: std::collections::HashSet<String> = std::collections::HashSet::new();
    for t in pub_map.keys() { all_topics.insert(t.clone()); }
    for t in sub_map.keys() { all_topics.insert(t.clone()); }

    let mut topics: Vec<TopicDef> = all_topics.into_iter().map(|name| {
        let (msg_type, hz, publisher) = pub_map.get(&name)
            .cloned()
            .unwrap_or(("std_msgs/String".into(), 1.0, "external".into()));
        let subscribers = sub_map.get(&name).cloned().unwrap_or_default();
        TopicDef { name, msg_type, publisher, subscribers, hz }
    }).collect();

    topics.sort_by(|a, b| a.name.cmp(&b.name));
    topics
}

fn guess_type_hz(topic: &str) -> (String, f32) {
    if topic.contains("image") { return ("sensor_msgs/Image".into(), 30.0); }
    if topic.contains("camera_info") { return ("sensor_msgs/CameraInfo".into(), 30.0); }
    if topic.contains("pointcloud") || topic.contains("point_cloud") {
        return ("sensor_msgs/PointCloud2".into(), 10.0);
    }
    if topic.contains("odometry") || topic.contains("odom") {
        return ("nav_msgs/Odometry".into(), 50.0);
    }
    if topic.contains("cmd_vel") { return ("geometry_msgs/Twist".into(), 20.0); }
    if topic.contains("scan") { return ("sensor_msgs/LaserScan".into(), 10.0); }
    if topic.contains("telemetry") { return ("std_msgs/String".into(), 2.0); }
    if topic.contains("status") { return ("parakram_msgs/DeviceStatus".into(), 1.0); }
    if topic.contains("detection") { return ("vision_msgs/Detection2DArray".into(), 10.0); }
    if topic.contains("fleet") { return ("parakram_msgs/FleetStatus".into(), 0.2); }
    if topic.contains("serial") { return ("parakram_msgs/SerialEvent".into(), 5.0); }
    if topic.contains("inference") || topic.contains("compile/ir") {
        return ("parakram_msgs/InferenceResult".into(), 1.0);
    }
    if topic.contains("joint") { return ("sensor_msgs/JointState".into(), 50.0); }
    if topic.contains("map") { return ("nav_msgs/OccupancyGrid".into(), 0.5); }
    if topic.contains("mesh") { return ("visualization_msgs/MarkerArray".into(), 1.0); }
    ("std_msgs/String".into(), 1.0)
}
