//! All known Isaac ROS + Parakram node graph templates.
//!
//! Each template encodes one domain (navigation, vision, sensing…).
//! The classifier scores them; the composer merges the winners.
//!
//! Adding a new domain = adding one `GraphTemplate` here. No other changes needed.

use super::NodeDef;
use serde_json::json;

/// One reusable domain pattern.
#[derive(Debug, Clone)]
pub struct GraphTemplate {
    pub name: &'static str,
    /// Words/phrases that strongly indicate this template is needed.
    /// Format: (word, weight). Weights sum to a match score.
    pub keywords: &'static [(&'static str, f32)],
    pub nodes: fn() -> Vec<NodeDef>,
}

// ── helpers ──────────────────────────────────────────────────────────────────

fn node(
    pkg: &str, exe: &str, name: &str,
    params: serde_json::Value,
    remappings: &[(&str, &str)],
    publishes: &[&str],
    subscribes: &[&str],
) -> NodeDef {
    NodeDef {
        package: pkg.into(), executable: exe.into(), name: name.into(),
        parameters: params,
        remappings: remappings.iter().map(|(a, b)| [a.to_string(), b.to_string()]).collect(),
        publishes: publishes.iter().map(|s| s.to_string()).collect(),
        subscribes: subscribes.iter().map(|s| s.to_string()).collect(),
    }
}

// ── templates ─────────────────────────────────────────────────────────────────

pub fn all_templates() -> Vec<GraphTemplate> {
    vec![

    // ── 1. Visual SLAM + Navigation ──────────────────────────────────────────
    GraphTemplate {
        name: "navigation_slam",
        keywords: &[
            ("navigate", 3.0), ("navigation", 3.0), ("nav", 2.0), ("move", 1.5),
            ("robot", 1.0), ("path", 2.0), ("map", 2.0), ("slam", 3.0),
            ("localiz", 2.5), ("obstacle", 2.0), ("waypoint", 2.5), ("autonomous", 2.0),
            ("drive", 1.5), ("follow", 1.5), ("patrol", 2.0), ("explore", 2.0),
        ],
        nodes: || vec![
            node("isaac_ros_visual_slam", "visual_slam_node", "vslam",
                json!({"enable_slam_visualization": true, "enable_localization_n_mapping": true}),
                &[("stereo_camera/left/image_raw", "/left/image_raw"),
                  ("stereo_camera/right/image_raw", "/right/image_raw")],
                &["/visual_slam/tracking/odometry", "/visual_slam/map_poses"],
                &["/left/image_raw", "/right/image_raw"]),

            node("nav2_bringup", "navigation_launch", "nav2",
                json!({"use_sim_time": false, "autostart": true}),
                &[("odom", "/visual_slam/tracking/odometry")],
                &["/plan", "/cmd_vel"],
                &["/visual_slam/tracking/odometry", "/map", "/scan"]),

            node("robot_localization", "ekf_node", "ekf_filter",
                json!({"frequency": 30.0, "sensor_timeout": 0.1,
                       "odom0": "/visual_slam/tracking/odometry",
                       "odom0_config": [true,true,false, false,false,false, true,true,false, false,false,true, false,false,false]}),
                &[], &["/odometry/filtered"], &["/visual_slam/tracking/odometry"]),

            node("parakram_ros", "device_manager", "parakram_device_manager",
                json!({"poll_rate_hz": 1.0}),
                &[], &["/parakram/fleet/status"], &["/parakram/command"]),
        ],
    },

    // ── 2. 3D Scene Reconstruction (nvblox) ──────────────────────────────────
    GraphTemplate {
        name: "3d_reconstruction",
        keywords: &[
            ("3d", 2.5), ("reconstruct", 3.0), ("map", 1.5), ("voxel", 3.0),
            ("nvblox", 3.0), ("depth", 2.0), ("point cloud", 2.5), ("lidar", 2.0),
            ("scan", 1.5), ("environment", 1.5), ("model", 1.5), ("mesh", 2.5),
        ],
        nodes: || vec![
            node("isaac_ros_nvblox", "nvblox_node", "nvblox",
                json!({"voxel_size": 0.05, "max_tsdf_update_hz": 10.0,
                       "max_color_update_hz": 5.0, "max_mesh_update_hz": 1.0}),
                &[],
                &["/nvblox_node/mesh", "/nvblox_node/map_slice", "/nvblox_node/esdf_pointcloud"],
                &["/depth_image", "/color_image", "/camera_info", "/transform"]),

            node("isaac_ros_image_proc", "rectify_node", "image_rectify",
                json!({}), &[],
                &["/image_rect"], &["/image_raw", "/camera_info"]),
        ],
    },

    // ── 3. DNN Object Detection / Inference ──────────────────────────────────
    GraphTemplate {
        name: "dnn_inference",
        keywords: &[
            ("detect", 2.5), ("recogni", 2.5), ("classify", 2.5), ("infer", 3.0),
            ("object", 2.0), ("model", 1.5), ("neural", 2.0), ("yolo", 3.0),
            ("person", 1.5), ("face", 2.0), ("anomaly", 2.5), ("defect", 2.5),
            ("quality", 1.5), ("vision", 1.5), ("camera", 1.0), ("image", 1.0),
            ("inspect", 2.0), ("count", 1.5),
        ],
        nodes: || vec![
            node("isaac_ros_dnn_inference", "triton_node", "dnn_inference",
                json!({"model_name": "peoplenet", "model_repository_paths": ["/tmp/models"],
                       "input_binding_names": ["input_1"], "output_binding_names": ["output_bbox/BiasAdd", "output_cov/Sigmoid"],
                       "inference_mode": "TritonClient", "max_batch_size": 8}),
                &[("image", "/image_rect")],
                &["/dnn_inference/raw_detections"],
                &["/image_rect"]),

            node("isaac_ros_image_proc", "rectify_node", "image_rectify",
                json!({}), &[],
                &["/image_rect"], &["/image_raw", "/camera_info"]),
        ],
    },

    // ── 4. Environmental Sensor Monitoring (Parakram + ROS) ──────────────────
    GraphTemplate {
        name: "sensor_monitoring",
        keywords: &[
            ("sensor", 2.0), ("monitor", 2.0), ("temperature", 2.0), ("humidity", 2.0),
            ("pressure", 1.5), ("air quality", 3.0), ("co2", 2.5), ("gas", 2.0),
            ("soil", 2.5), ("moisture", 2.5), ("light", 1.5), ("lux", 2.0),
            ("telemetry", 2.0), ("read", 1.0), ("measure", 1.5), ("log", 1.0),
            ("alert", 2.0), ("threshold", 2.5), ("alarm", 2.0),
        ],
        nodes: || vec![
            node("parakram_ros", "device_manager", "parakram_device_manager",
                json!({"poll_rate_hz": 2.0}),
                &[],
                &["/parakram/fleet/status"],
                &["/parakram/command"]),

            node("parakram_ros", "cloud_sync", "parakram_cloud_sync",
                json!({"batch_size": 100, "sync_interval_s": 10.0}),
                &[], &[], &["/parakram/fleet/status"]),
        ],
    },

    // ── 5. Irrigation / Agricultural Robot ───────────────────────────────────
    GraphTemplate {
        name: "agricultural_robot",
        keywords: &[
            ("irrigation", 3.0), ("water", 2.0), ("plant", 2.5), ("crop", 2.5),
            ("soil", 2.5), ("moisture", 2.5), ("farm", 2.5), ("garden", 2.0),
            ("greenhouse", 2.5), ("fertiliz", 2.0), ("harvest", 2.5), ("weed", 2.5),
            ("agricultural", 3.0), ("field", 1.5), ("spray", 2.0),
        ],
        nodes: || vec![
            node("parakram_ros", "device_manager", "parakram_device_manager",
                json!({"poll_rate_hz": 0.5}), &[],
                &["/parakram/fleet/status"], &["/parakram/command"]),

            node("parakram_ros", "fleet_orchestrator", "parakram_fleet",
                json!({"strategy": "zone", "health_publish_rate_hz": 0.2}),
                &[],
                &["/parakram/fleet/health"],
                &["/parakram/fleet/status", "/parakram/compile/ir"]),

            node("parakram_ros", "isaac_sim_bridge", "parakram_sim_bridge",
                json!({"sim_speed": 10.0}), &[],
                &["/parakram/sim/sensor_data"], &[]),

            node("isaac_ros_visual_slam", "visual_slam_node", "vslam",
                json!({"enable_slam_visualization": false, "enable_localization_n_mapping": true}),
                &[], &["/visual_slam/tracking/odometry"], &["/left/image_raw", "/right/image_raw"]),

            node("nav2_bringup", "navigation_launch", "nav2",
                json!({"use_sim_time": false, "autostart": true}),
                &[("odom", "/visual_slam/tracking/odometry")],
                &["/cmd_vel"], &["/visual_slam/tracking/odometry"]),
        ],
    },

    // ── 6. Security & Surveillance ───────────────────────────────────────────
    GraphTemplate {
        name: "security_surveillance",
        keywords: &[
            ("security", 3.0), ("surveillance", 3.0), ("intruder", 3.0), ("motion", 2.0),
            ("pir", 2.0), ("camera", 1.5), ("detect person", 3.0), ("intrusion", 3.0),
            ("alarm", 2.5), ("alert", 2.0), ("guard", 2.5), ("watch", 1.5),
            ("perimeter", 2.5), ("lock", 1.5), ("access", 1.5),
        ],
        nodes: || vec![
            node("parakram_ros", "device_manager", "parakram_device_manager",
                json!({"poll_rate_hz": 5.0}), &[],
                &["/parakram/fleet/status"], &["/parakram/command"]),

            node("isaac_ros_dnn_inference", "triton_node", "person_detector",
                json!({"model_name": "peoplenet", "model_repository_paths": ["/tmp/models"],
                       "input_binding_names": ["input_1"],
                       "output_binding_names": ["output_bbox/BiasAdd", "output_cov/Sigmoid"],
                       "inference_mode": "TritonClient", "max_batch_size": 4}),
                &[("image", "/image_rect")],
                &["/security/detections"], &["/image_rect"]),

            node("isaac_ros_image_proc", "rectify_node", "image_rectify",
                json!({}), &[], &["/image_rect"], &["/image_raw", "/camera_info"]),

            node("parakram_ros", "serial_monitor", "parakram_serial",
                json!({"auto_debug": false, "baud": 115200}),
                &[], &["/parakram/serial/events"], &[]),
        ],
    },

    // ── 7. Industrial Quality Control ────────────────────────────────────────
    GraphTemplate {
        name: "industrial_quality",
        keywords: &[
            ("quality", 2.5), ("defect", 3.0), ("inspect", 2.5), ("conveyor", 3.0),
            ("manufacturing", 2.5), ("assembly", 2.5), ("production", 2.0),
            ("industrial", 2.0), ("anomaly", 3.0), ("reject", 2.5), ("pass", 1.5),
            ("fail", 1.5), ("measurement", 2.0), ("tolerance", 2.5), ("vision", 1.5),
        ],
        nodes: || vec![
            node("isaac_ros_dnn_inference", "triton_node", "defect_detector",
                json!({"model_name": "industrial_anomaly", "model_repository_paths": ["/tmp/models"],
                       "inference_mode": "TritonClient", "max_batch_size": 8}),
                &[("image", "/image_rect")],
                &["/quality/defect_detections"], &["/image_rect"]),

            node("isaac_ros_image_proc", "rectify_node", "image_rectify",
                json!({}), &[], &["/image_rect"], &["/image_raw", "/camera_info"]),

            node("parakram_ros", "device_manager", "parakram_device_manager",
                json!({"poll_rate_hz": 10.0}), &[],
                &["/parakram/fleet/status"], &["/parakram/command"]),

            node("parakram_ros", "fleet_orchestrator", "parakram_fleet",
                json!({"strategy": "broadcast"}), &[],
                &["/parakram/fleet/health"], &["/parakram/fleet/status"]),
        ],
    },

    // ── 8. Cold Chain / Logistics Temperature Monitoring ─────────────────────
    GraphTemplate {
        name: "cold_chain",
        keywords: &[
            ("cold chain", 4.0), ("refrigerat", 3.0), ("freezer", 3.0), ("temperature", 2.0),
            ("logistics", 2.5), ("warehouse", 2.5), ("storage", 2.0), ("perishable", 3.0),
            ("food", 2.0), ("pharma", 2.5), ("medicine", 2.0), ("vaccine", 3.0),
            ("excursion", 3.0), ("compliance", 2.0),
        ],
        nodes: || vec![
            node("parakram_ros", "device_manager", "parakram_device_manager",
                json!({"poll_rate_hz": 0.2}), &[],
                &["/parakram/fleet/status"], &["/parakram/command"]),

            node("parakram_ros", "fleet_orchestrator", "parakram_fleet",
                json!({"strategy": "zone", "health_publish_rate_hz": 0.1}), &[],
                &["/parakram/fleet/health"], &["/parakram/fleet/status"]),

            node("parakram_ros", "cloud_sync", "parakram_cloud_sync",
                json!({"batch_size": 500, "sync_interval_s": 30.0,
                       "ota_poll_interval_s": 300.0}), &[], &[], &["/parakram/fleet/status"]),
        ],
    },

    // ── 9. Manipulation / Pick and Place ─────────────────────────────────────
    GraphTemplate {
        name: "manipulation",
        keywords: &[
            ("pick", 2.5), ("place", 2.5), ("manipulat", 3.0), ("arm", 2.0),
            ("grasp", 3.0), ("grip", 2.5), ("robot arm", 3.0), ("joint", 2.0),
            ("moveit", 3.0), ("trajectory", 2.5), ("end effector", 3.0),
            ("bin picking", 3.0), ("sort", 2.0), ("assemble", 2.0),
        ],
        nodes: || vec![
            node("isaac_ros_dnn_inference", "triton_node", "grasp_detector",
                json!({"model_name": "grasp_net", "model_repository_paths": ["/tmp/models"],
                       "inference_mode": "TritonClient"}),
                &[("image", "/image_rect")],
                &["/grasp/poses"], &["/image_rect", "/depth_image"]),

            node("isaac_ros_image_proc", "rectify_node", "image_rectify",
                json!({}), &[], &["/image_rect"], &["/image_raw", "/camera_info"]),

            node("moveit_ros_move_group", "move_group", "move_group",
                json!({"use_sim_time": false, "publish_monitored_planning_scene": true}),
                &[], &["/move_group/status"], &["/grasp/poses", "/joint_states"]),

            node("parakram_ros", "device_manager", "parakram_device_manager",
                json!({"poll_rate_hz": 1.0}), &[],
                &["/parakram/fleet/status"], &["/parakram/command"]),
        ],
    },

    // ── 10. Fleet / Multi-Robot Coordination ─────────────────────────────────
    GraphTemplate {
        name: "fleet_coordination",
        keywords: &[
            ("fleet", 3.0), ("multi robot", 3.0), ("swarm", 3.0), ("coordinate", 2.5),
            ("multiple device", 3.0), ("broadcast", 2.5), ("role", 2.0), ("zone", 2.0),
            ("formation", 2.5), ("dispatch", 2.0), ("assign", 2.0), ("orchestrat", 3.0),
        ],
        nodes: || vec![
            node("parakram_ros", "device_manager", "parakram_device_manager",
                json!({"poll_rate_hz": 2.0}), &[],
                &["/parakram/fleet/status"], &["/parakram/command"]),

            node("parakram_ros", "fleet_orchestrator", "parakram_fleet",
                json!({"strategy": "role", "health_publish_rate_hz": 1.0}), &[],
                &["/parakram/fleet/health"], &["/parakram/fleet/status", "/parakram/compile/ir"]),

            node("parakram_ros", "neural_compiler", "parakram_neural_compiler",
                json!({"onnx_model_path": "", "confidence_threshold": 0.7,
                       "cloud_fallback": true}),
                &[], &["/parakram/compile/ir"], &["/parakram/compile/prompt"]),
        ],
    },

    // ── 11. AprilTag / Marker-Based Localization ──────────────────────────────
    GraphTemplate {
        name: "apriltag_localization",
        keywords: &[
            ("apriltag", 4.0), ("fiducial", 3.0), ("marker", 2.5), ("tag", 2.0),
            ("localiz", 2.0), ("aruco", 3.0), ("dock", 2.5), ("station", 2.0),
            ("charger", 2.0), ("landmark", 2.5),
        ],
        nodes: || vec![
            node("isaac_ros_apriltag", "isaac_ros_apriltag", "apriltag_detector",
                json!({"size": 0.22, "max_tags": 64}),
                &[("image", "/image_rect"), ("camera_info", "/camera_info")],
                &["/tag_detections"], &["/image_rect", "/camera_info"]),

            node("isaac_ros_image_proc", "rectify_node", "image_rectify",
                json!({}), &[], &["/image_rect"], &["/image_raw", "/camera_info"]),
        ],
    },

    // ── 12. Health / Medical IoT ──────────────────────────────────────────────
    GraphTemplate {
        name: "health_monitoring",
        keywords: &[
            ("health", 2.5), ("medical", 3.0), ("patient", 3.0), ("heart rate", 3.0),
            ("spo2", 3.0), ("oxygen", 2.5), ("blood pressure", 3.0), ("vital", 3.0),
            ("hospital", 3.0), ("clinic", 2.5), ("wearable", 2.5), ("ecg", 3.0),
        ],
        nodes: || vec![
            node("parakram_ros", "device_manager", "parakram_device_manager",
                json!({"poll_rate_hz": 5.0}), &[],
                &["/parakram/fleet/status"], &["/parakram/command"]),

            node("parakram_ros", "cloud_sync", "parakram_cloud_sync",
                json!({"batch_size": 200, "sync_interval_s": 5.0}),
                &[], &[], &["/parakram/fleet/status"]),

            node("parakram_ros", "serial_monitor", "parakram_serial",
                json!({"auto_debug": true, "baud": 115200}),
                &[], &["/parakram/serial/events"], &[]),
        ],
    },

    // ── 13. Simulation / Digital Twin ────────────────────────────────────────
    GraphTemplate {
        name: "simulation_digital_twin",
        keywords: &[
            ("simulat", 3.0), ("digital twin", 4.0), ("virtual", 2.5), ("isaac sim", 4.0),
            ("omniverse", 3.5), ("test", 1.5), ("replay", 2.5), ("synthetic", 3.0),
            ("validate", 2.0), ("prototype", 2.0), ("virtual sensor", 3.0),
        ],
        nodes: || vec![
            node("parakram_ros", "isaac_sim_bridge", "parakram_sim_bridge",
                json!({"sim_speed": 10.0}), &[],
                &["/parakram/sim/sensor_data"], &[]),

            node("parakram_ros", "neural_compiler", "parakram_neural_compiler",
                json!({"onnx_model_path": "", "confidence_threshold": 0.7, "cloud_fallback": false}),
                &[], &["/parakram/compile/ir"], &["/parakram/compile/prompt"]),

            node("parakram_ros", "device_manager", "parakram_device_manager",
                json!({"poll_rate_hz": 10.0}), &[],
                &["/parakram/fleet/status"], &["/parakram/command"]),
        ],
    },

    // ── 14. Serial / UART Auto-Debug ─────────────────────────────────────────
    GraphTemplate {
        name: "serial_debug",
        keywords: &[
            ("serial", 2.5), ("uart", 3.0), ("debug", 2.5), ("crash", 3.0),
            ("panic", 3.0), ("log", 2.0), ("firmware", 2.0), ("flash", 2.0),
            ("monitor", 2.0), ("error", 1.5), ("stack overflow", 3.0), ("backtrace", 3.0),
        ],
        nodes: || vec![
            node("parakram_ros", "serial_monitor", "parakram_serial",
                json!({"auto_debug": true, "baud": 115200, "max_debug_iterations": 3}),
                &[],
                &["/parakram/serial/events", "/parakram/serial/errors", "/parakram/serial/debug_suggestions"],
                &[]),

            node("parakram_ros", "device_manager", "parakram_device_manager",
                json!({"poll_rate_hz": 1.0}), &[],
                &["/parakram/fleet/status"], &["/parakram/command"]),
        ],
    },

    // ── 15. Power / Energy Management ────────────────────────────────────────
    GraphTemplate {
        name: "power_management",
        keywords: &[
            ("power", 2.5), ("energy", 2.5), ("solar", 3.0), ("battery", 2.5),
            ("electricity", 2.0), ("current", 2.0), ("voltage", 2.0), ("watt", 2.5),
            ("consumption", 2.5), ("efficiency", 2.0), ("grid", 2.5), ("load", 2.0),
        ],
        nodes: || vec![
            node("parakram_ros", "device_manager", "parakram_device_manager",
                json!({"poll_rate_hz": 1.0}), &[],
                &["/parakram/fleet/status"], &["/parakram/command"]),

            node("parakram_ros", "cloud_sync", "parakram_cloud_sync",
                json!({"batch_size": 200, "sync_interval_s": 10.0}),
                &[], &[], &["/parakram/fleet/status"]),
        ],
    },

    ]
}
