# Distributed Network File System with Timeout-based Caching

A network file system (based on remote access and upload-download models) that supports concurrent readers (multiple) and writers (one) with **C/C++, FUSE (libfuse), reader-writer locks, remote-procedure calls (built using TCP sockets), and syscalls**. For the upload-download model, implemented a **timeout-based client-side caching scheme** to ensure file freshness.