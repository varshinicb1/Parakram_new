.PHONY: test test-unit test-integration

test: test-unit test-integration

test-unit:
	cd .. && cargo test

test-integration:
	bash integration_test.sh
