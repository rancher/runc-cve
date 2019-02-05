release:
	docker build -t run-cve .
	docker create --name run-cve run-cve
	docker cp run-cve:/go/src/github.com/opencontainers/runc/dist .
	docker rm -fv run-cve
