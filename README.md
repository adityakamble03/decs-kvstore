 Key-Value Store Server + Load Generator

A high-performance, multi-threaded HTTP Key-Value Store implemented in C++ with PostgreSQL persistence and a standalone C++ load generator for benchmark testing.

This project is designed for systems, databases, caching, and performance engineering experiments.

Features
Key-Value HTTP Server

Built using cpp-httplib

Thread-safe and multi-threaded

Integrated LRU Cache

Persistent storage via PostgreSQL (using libpqxx)

Endpoints:

POST /create

GET /read?key=<key>

DELETE /delete?key=<key>

Configurable via environment variables

Load Generator (Benchmark Tool)

A high-performance, multi-threaded client that simulates real workloads.

Supported Workloads
Workload	Description
put_all	Only writes/deletes → DB-heavy
get_all	GET requests on unique keys → Cache-miss heavy
get_popular	Repeated access to a hot keyset → Cache-hit heavy
get_put	Mixed load with configurable read/write ratio