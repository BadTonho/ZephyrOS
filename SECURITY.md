# Security Policy

## Supported Versions

ZephyrOS is an operating system under active development. Security updates, bug fixes, and stability improvements are applied directly to the main development branch.

| Version / Branch | Supported          | Status |
| ---------------- | ------------------ | ------ |
| `main` (Latest)  | :white_check_mark: | Active development & security fixes |
| Older releases   | :x:                | Not supported (please update to `main`) |

---

## Scope & Nature of the Project

ZephyrOS is a modular 32-bit x86 operating system written from scratch and under active development toward real hardware and user deployments. We prioritize memory safety, reliable bounds checking, and strict isolation between Ring 0 (Kernel) and Ring 3 (User Space). QEMU and Bochs are the current validation environments while hardware support, security review, and recovery procedures continue to mature.

---

## Reporting a Vulnerability

If you discover a security vulnerability, buffer overflow, privilege escalation issue at the syscall boundary, or any memory corruption flaw:

1. **Private Disclosure**: Please report the vulnerability through [GitHub Security Advisories](https://github.com/BadTonho/ZephyrOS/security/advisories/new) or contact the maintainers directly.
2. **Details to Include**:
   - A clear description of the vulnerability and affected subsystem (e.g. `src/core/syscall.c`, memory allocator, paging, filesystem drivers).
   - Steps to reproduce the issue or minimal Proof-of-Concept (PoC) code/binary running under QEMU.
   - Any suggested patch or remediation (if available).
3. **Response Timeline**:
   - **Acknowledgment**: Within 48–72 hours.
   - **Fix & Disclosure**: We will triage the issue, develop a patch in a private branch, and release the fix directly to `main`.

Thank you for helping keep ZephyrOS safe and reliable!
