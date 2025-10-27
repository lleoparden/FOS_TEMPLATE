# 🧠 FOS_TEMPLATE – Educational Operating System Framework  
### Team 95 — FOS Project 2025  

---

## 📖 About This Repository
This repository hosts **Team 95’s implementation** of the **FOS (Fundamentals of Operating Systems) Project 2025**.  
It is built upon the official **FOS educational OS framework**, which provides a modular environment for developing and experimenting with core operating system mechanisms.

The project walks through all fundamental OS concepts — from **bootloading and memory management** to **CPU scheduling, protection, and system calls** — allowing a practical and structured approach to learning how real operating systems work.

This repository represents our team’s full implementation and integration of all required **group modules** and **individual modules**, developed collaboratively and tested under **Bochs** and **GCC** environments.

---

## 🧩 System Architecture Overview

```
FOS_TEMPLATE/
 ┣ boot/     → Bootloader & low-level initialization
 ┣ kern/     → Kernel core (CPU, memory, scheduler, traps, faults, etc.)
 ┣ lib/      → User-space library (system calls, I/O, allocators)
 ┣ user/     → User applications, demos, and test programs
 ┣ inc/      → Shared header files between kernel & user
 ┣ conf/     → Build and environment configurations
 ┣ obj/      → Compiled binaries and temporary build files
 ┗ tools/    → Utility scripts and configuration helpers
```

At runtime, the **bootloader** initializes the CPU and memory, loads the **kernel** into memory, and transfers control to `init.c`.  
The kernel then sets up **paging**, **interrupts**, and **scheduling**, before launching user-mode programs compiled in `user/`.

---

## ⚙️ Key Features
- Modular kernel supporting **incremental development**
- **Dynamic allocation** and **kernel heap management**
- **User heap** allocator and shared memory system
- **Paging** and **pagefile management**
- **CPU scheduling algorithms** and context switching
- **Fault handling framework** with trap and interrupt management
- **Kernel protection** and privilege-level separation
- **Synchronization primitives:** spinlocks, semaphores, sleep locks
- **System call interface** for user–kernel communication
- Comprehensive **testing suite** for validation

---

## 🧠 Development Goals
This project aims to:
- Illustrate the **core principles** behind modern operating systems  
- Provide hands-on experience with **kernel-level C and Assembly programming**  
- Emphasize **modular development** — each subsystem is independently testable  
- Encourage **logical reasoning** and **design correctness** over test-passing  
- Build a functional educational OS with memory safety, concurrency, and isolation  

---

## 👥 Team 95 – Contributors & Modules

| Member | Group Module(s) | Individual Module | Key Focus / Contribution |
|---------|------------------|------------------|---------------------------|
| **Mostafa Eid** | Dynamic Allocation | Fault Handling 3 | Designed dynamic memory allocation mechanisms and implemented advanced page fault handling |
| **Mostafa Kafrawy** | Kernel Heap | Memory Management | Built kernel heap management and developed virtual memory subsystem |
| **Yassin Marwan** | Fault Handling 1 | Fault Handling 2 | Implemented core fault detection and exception resolution across trap handlers |
| **Yassin Wahb** | Fault Handling | Kernel Protection | Worked on exception handling flow and kernel protection mechanisms |
| **Nour Diaa** | Kernel Heap | User Heap | Developed kernel and user heap allocators with safe dynamic memory interfaces |
| **Nadeen Hassan** | Dynamic Allocation | CPU Scheduling | Designed the process scheduling subsystem and integrated dynamic allocation layer |

> 🧩 Each member contributed to both **team modules** and **individual modules**, ensuring correct integration, synchronization, and testing across all kernel layers.

---

## 🧠 Educational Objectives
This repository serves as both a **teaching tool** and a **research sandbox**, covering:
- Booting and system initialization  
- Virtual memory & address translation  
- Page tables and working sets  
- Scheduling algorithms & context switching  
- Inter-process communication and protection  
- Synchronization primitives and critical sections  

It provides a hands-on experience that connects OS theory to actual system-level implementation.

---

## 📄 License & Acknowledgments
- Based on the **FOS 2025 Educational OS Framework**  
- Licensed for academic and non-commercial use only  
- Includes modernized support for GCC toolchains  
- Special thanks to the course staff and mentors for guidance and support  

---

> **Team 95** — *Mostafa Eid, Mostafa Kafrawy, Yassin Marwan, Yassin Wahb, Nour Diaa, Nadeen Hassan*
