# Simple C HTTP Server (Thread Pool)

A tiny HTTP/1.1 server written in C using POSIX sockets + pthreads.

## What it does

-   Listens on **port 8080**
-   Handles **GET** requests (other methods return **405**)
-   Serves HTML files from the `pages/` folder
-   Basic routing:
    -   `/` -> `pages/index.html`
    -   `/something` -> `pages/something/index.html`
-   Returns a custom 404 page from `pages/error.html` (or a fallback 404 if missing)
-   Supports `Connection: keep-alive` (otherwise closes the connection)

## Folder structure (expected)
