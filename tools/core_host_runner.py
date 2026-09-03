#!/usr/bin/env python3
"""Executa a primeira bateria host-only com evidência dinâmica de cobertura."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools import coverage_collector


RESULT_DIR = ROOT / "build" / "test-results" / "core-host"
DEFAULT_BINARY = ROOT / "build" / "tests" / "test_core_contracts_host.exe"
NETWORK_RESULT_DIR = ROOT / "build" / "test-results" / "network-host"
NETWORK_BINARY = ROOT / "build" / "tests" / "test_network_host.exe"
NETWORK_MANAGER_RESULT_DIR = ROOT / "build" / "test-results" / "network-manager-host"
NETWORK_MANAGER_BINARY = ROOT / "build" / "tests" / "test_network_manager_host.exe"
ROUTE_RESULT_DIR = ROOT / "build" / "test-results" / "route-host"
ROUTE_BINARY = ROOT / "build" / "tests" / "test_route_host.exe"
IPV4_RESULT_DIR = ROOT / "build" / "test-results" / "ipv4-host"
IPV4_BINARY = ROOT / "build" / "tests" / "test_ipv4_host.exe"
CRYPTO_RESULT_DIR = ROOT / "build" / "test-results" / "crypto-host"
CRYPTO_BINARY = ROOT / "build" / "tests" / "test_crypto_host.exe"
SCHEDULING_RESULT_DIR = ROOT / "build" / "test-results" / "scheduling-host"
SCHEDULING_BINARY = ROOT / "build" / "tests" / "test_core_scheduling_host.exe"
PACKAGE_RESULT_DIR = ROOT / "build" / "test-results" / "package-host"
PACKAGE_BINARY = ROOT / "build" / "tests" / "test_package_host.exe"
STATE_RESULT_DIR = ROOT / "build" / "test-results" / "state-host"
STATE_BINARY = ROOT / "build" / "tests" / "test_core_state_host.exe"
DEVICE_RESULT_DIR = ROOT / "build" / "test-results" / "device-manager-host"
DEVICE_BINARY = ROOT / "build" / "tests" / "test_device_manager_host.exe"
APP_API_RESULT_DIR = ROOT / "build" / "test-results" / "app-api-host"
APP_API_BINARY = ROOT / "build" / "tests" / "test_app_api_host.exe"
APP_FILES_RESULT_DIR = ROOT / "build" / "test-results" / "app-files-host"
APP_FILES_BINARY = ROOT / "build" / "tests" / "test_app_files_host.exe"
APP_BUILTIN_RESULT_DIR = ROOT / "build" / "test-results" / "app-builtin-host"
APP_BUILTIN_BINARY = ROOT / "build" / "tests" / "test_app_builtin_host.exe"
APP_CATALOG_RESULT_DIR = ROOT / "build" / "test-results" / "app-catalog-host"
APP_CATALOG_BINARY = ROOT / "build" / "tests" / "test_app_catalog_host.exe"
INPUT_RESULT_DIR = ROOT / "build" / "test-results" / "input-host"
INPUT_BINARY = ROOT / "build" / "tests" / "test_input_host.exe"
POWER_RESULT_DIR = ROOT / "build" / "test-results" / "power-host"
POWER_BINARY = ROOT / "build" / "tests" / "test_power_host.exe"
VFS_PATH_RESULT_DIR = ROOT / "build" / "test-results" / "vfs-path-host"
VFS_PATH_BINARY = ROOT / "build" / "tests" / "test_vfs_path_host.exe"
FILE_INDEX_RESULT_DIR = ROOT / "build" / "test-results" / "file-index-host"
FILE_INDEX_BINARY = ROOT / "build" / "tests" / "test_file_index_host.exe"
FS_RESULT_DIR = ROOT / "build" / "test-results" / "fs-host"
FS_BINARY = ROOT / "build" / "tests" / "test_fs_host.exe"
STORAGE_RESULT_DIR = ROOT / "build" / "test-results" / "storage-host"
STORAGE_BINARY = ROOT / "build" / "tests" / "test_storage_host.exe"
BLOCK_RESULT_DIR = ROOT / "build" / "test-results" / "block-host"
BLOCK_BINARY = ROOT / "build" / "tests" / "test_block_host.exe"
FAT12_RESULT_DIR = ROOT / "build" / "test-results" / "fat12-host"
FAT12_BINARY = ROOT / "build" / "tests" / "test_fat12_host.exe"
FAT32_RESULT_DIR = ROOT / "build" / "test-results" / "fat32-host"
FAT32_BINARY = ROOT / "build" / "tests" / "test_fat32_host.exe"
VFS_RESULT_DIR = ROOT / "build" / "test-results" / "vfs-host"
VFS_BINARY = ROOT / "build" / "tests" / "test_vfs_host.exe"
SLAB_RESULT_DIR = ROOT / "build" / "test-results" / "slab-host"
SLAB_BINARY = ROOT / "build" / "tests" / "test_slab_metadata_host.exe"
TIMER_RESULT_DIR = ROOT / "build" / "test-results" / "timer-host"
TIMER_BINARY = ROOT / "build" / "tests" / "test_timer_host.exe"
UDP_RESULT_DIR = ROOT / "build" / "test-results" / "udp-host"
UDP_BINARY = ROOT / "build" / "tests" / "test_udp_host.exe"
ARP_RESULT_DIR = ROOT / "build" / "test-results" / "arp-host"
ARP_BINARY = ROOT / "build" / "tests" / "test_arp_host.exe"
ICMP_RESULT_DIR = ROOT / "build" / "test-results" / "icmp-host"
ICMP_BINARY = ROOT / "build" / "tests" / "test_icmp_host.exe"
DNS_RESULT_DIR = ROOT / "build" / "test-results" / "dns-host"
DNS_BINARY = ROOT / "build" / "tests" / "test_dns_host.exe"
DHCP_RESULT_DIR = ROOT / "build" / "test-results" / "dhcp-host"
DHCP_BINARY = ROOT / "build" / "tests" / "test_dhcp_host.exe"
ETHERNET_RESULT_DIR = ROOT / "build" / "test-results" / "ethernet-host"
ETHERNET_BINARY = ROOT / "build" / "tests" / "test_ethernet_host.exe"
TCP_RESULT_DIR = ROOT / "build" / "test-results" / "tcp-host"
TCP_BINARY = ROOT / "build" / "tests" / "test_tcp_host.exe"
TLS_RESULT_DIR = ROOT / "build" / "test-results" / "tls-host"
TLS_BINARY = ROOT / "build" / "tests" / "test_tls_host.exe"
HTTP_RESULT_DIR = ROOT / "build" / "test-results" / "http-host"
HTTP_BINARY = ROOT / "build" / "tests" / "test_http_host.exe"
NET_SOCKET_RESULT_DIR = ROOT / "build" / "test-results" / "net-socket-host"
NET_SOCKET_BINARY = ROOT / "build" / "tests" / "test_net_socket_host.exe"
VMA_RESULT_DIR = ROOT / "build" / "test-results" / "vma-host"
VMA_BINARY = ROOT / "build" / "tests" / "test_vma_host.exe"
PAGING_RESULT_DIR = ROOT / "build" / "test-results" / "paging-host"
PAGING_BINARY = ROOT / "build" / "tests" / "test_paging_host.exe"
MEMORY_RESULT_DIR = ROOT / "build" / "test-results" / "memory-host"
MEMORY_BINARY = ROOT / "build" / "tests" / "test_memory_host.exe"
SIGNAL_RESULT_DIR = ROOT / "build" / "test-results" / "process-signal-host"
SIGNAL_BINARY = ROOT / "build" / "tests" / "test_process_signal_host.exe"
IPC_RESULT_DIR = ROOT / "build" / "test-results" / "process-ipc-host"
IPC_BINARY = ROOT / "build" / "tests" / "test_process_ipc_host.exe"
WORKQUEUE_RESULT_DIR = ROOT / "build" / "test-results" / "workqueue-host"
WORKQUEUE_BINARY = ROOT / "build" / "tests" / "test_workqueue_host.exe"
BEARSSL_COMPAT_RESULT_DIR = ROOT / "build" / "test-results" / "bearssl-compat-host"
BEARSSL_COMPAT_BINARY = ROOT / "build" / "tests" / "test_bearssl_compat_host.exe"
SHELL_DISPATCH_RESULT_DIR = ROOT / "build" / "test-results" / "shell-dispatch-host"
SHELL_DISPATCH_BINARY = ROOT / "build" / "tests" / "test_shell_dispatch_host.exe"
DEFAULT_TIMEOUT = 120.0
CORE_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_core_contracts.c",
    ROOT / "src" / "core" / "string.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "clock.c",
    ROOT / "src" / "core" / "test_protocol_core.c",
)
NETWORK_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_network_host.c",
    ROOT / "src" / "core" / "net_buffer.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
NETWORK_MANAGER_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_network_manager_host.c",
    ROOT / "src" / "core" / "network_manager.c",
    ROOT / "src" / "core" / "recovery.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
ROUTE_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_route_host.c",
    ROOT / "src" / "core" / "route.c",
    ROOT / "src" / "core" / "ipv4.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
IPV4_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_ipv4_host.c",
    ROOT / "src" / "core" / "ipv4.c",
    ROOT / "src" / "core" / "route.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
CRYPTO_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_crypto_host.c",
    ROOT / "src" / "core" / "crypto.c",
    ROOT / "src" / "core" / "crypto_ed25519.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
SCHEDULING_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_core_scheduling_host.c",
    ROOT / "src" / "core" / "wait.c",
    ROOT / "src" / "core" / "workqueue.c",
    ROOT / "src" / "core" / "irq_deferred.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
PACKAGE_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_package_host.c",
    ROOT / "src" / "core" / "app_package.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
STATE_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_core_state_host.c",
    ROOT / "src" / "core" / "recovery.c",
    ROOT / "src" / "core" / "power_notifier.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
DEVICE_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_device_manager_host.c",
    ROOT / "src" / "core" / "device_manager.c",
    ROOT / "src" / "core" / "recovery.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
APP_API_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_app_api_host.c",
    ROOT / "src" / "core" / "app_api.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
APP_FILES_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_app_files_host.c",
    ROOT / "src" / "core" / "app_files.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
APP_BUILTIN_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_app_builtin_host.c",
    ROOT / "src" / "core" / "app_builtin.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
APP_CATALOG_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_app_catalog_host.c",
    ROOT / "src" / "core" / "app_catalog.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
INPUT_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_input_host.c",
    ROOT / "src" / "core" / "input.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
POWER_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_power_host.c",
    ROOT / "src" / "core" / "power.c",
    ROOT / "src" / "core" / "power_notifier.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
VFS_PATH_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_vfs_path_host.c",
    ROOT / "src" / "fs" / "vfs_path.c",
    ROOT / "src" / "core" / "string.c",
)
FILE_INDEX_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_file_index_host.c",
    ROOT / "src" / "fs" / "file_index.c",
    ROOT / "src" / "core" / "string.c",
)
FS_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_fs_host.c",
    ROOT / "src" / "fs" / "fs.c",
    ROOT / "src" / "core" / "string.c",
)
STORAGE_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_storage_host.c",
    ROOT / "src" / "fs" / "storage.c",
    ROOT / "src" / "core" / "string.c",
)
BLOCK_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_block_host.c",
    ROOT / "src" / "fs" / "block.c",
    ROOT / "src" / "fs" / "block_cache.c",
    ROOT / "src" / "core" / "string.c",
)
FAT12_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_fat12_host.c",
    ROOT / "src" / "fs" / "fat12.c",
    ROOT / "src" / "core" / "string.c",
)
FAT32_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_fat32_host.c",
    ROOT / "src" / "fs" / "fat32.c",
    ROOT / "src" / "core" / "string.c",
)
VFS_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_vfs_host.c",
    ROOT / "src" / "fs" / "vfs.c",
    ROOT / "src" / "core" / "string.c",
)
SLAB_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_slab_metadata_host.c",
    ROOT / "src" / "memory" / "slab.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
TIMER_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_timer_host.c",
    ROOT / "src" / "drivers" / "timer.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
UDP_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_udp_host.c",
    ROOT / "src" / "core" / "udp.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
ARP_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_arp_host.c",
    ROOT / "src" / "core" / "arp.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
ICMP_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_icmp_host.c",
    ROOT / "src" / "core" / "icmp.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
DNS_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_dns_host.c",
    ROOT / "src" / "core" / "dns.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
DHCP_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_dhcp_host.c",
    ROOT / "src" / "core" / "dhcp.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
ETHERNET_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_ethernet_host.c",
    ROOT / "src" / "core" / "ethernet.c",
    ROOT / "src" / "core" / "sk_buff.c",
    ROOT / "src" / "core" / "net_buffer.c",
    ROOT / "src" / "memory" / "slab.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
TCP_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_tcp_host.c",
    ROOT / "src" / "core" / "tcp.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
TLS_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_tls_host.c",
    ROOT / "src" / "core" / "tls.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
HTTP_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_http_host.c",
    ROOT / "src" / "core" / "http.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
NET_SOCKET_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_net_socket_host.c",
    ROOT / "src" / "core" / "net_socket.c",
    ROOT / "src" / "core" / "wait.c",
    ROOT / "src" / "core" / "net_buffer.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
VMA_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_vma_host.c",
    ROOT / "src" / "memory" / "vma.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
PAGING_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_paging_host.c",
    ROOT / "src" / "memory" / "paging.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
MEMORY_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_memory_host.c",
    ROOT / "src" / "memory" / "memory.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
SIGNAL_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_process_signal_host.c",
    ROOT / "src" / "process" / "signal.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
IPC_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_process_ipc_host.c",
    ROOT / "src" / "process" / "ipc.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
WORKQUEUE_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_workqueue_host.c",
    ROOT / "src" / "core" / "workqueue.c",
    ROOT / "src" / "core" / "log.c",
    ROOT / "src" / "core" / "string.c",
)
BEARSSL_COMPAT_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_bearssl_compat_host.c",
    ROOT / "src" / "core" / "bearssl_compat.c",
)
SHELL_DISPATCH_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_shell_dispatch_host.c",
    ROOT / "src" / "shell" / "shell_dispatch.c",
    ROOT / "src" / "core" / "string.c",
)
SHELL_INTROSPECTION_RESULT_DIR = ROOT / "build" / "test-results" / "shell-introspection-host"
SHELL_INTROSPECTION_BINARY = ROOT / "build" / "tests" / "test_shell_introspection_host.exe"
SHELL_INTROSPECTION_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_shell_introspection_host.c",
    ROOT / "src" / "shell" / "shell_introspection.c",
    ROOT / "src" / "core" / "string.c",
)
FONT_RESULT_DIR = ROOT / "build" / "test-results" / "font-host"
FONT_BINARY = ROOT / "build" / "tests" / "test_font_host.exe"
FONT_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_font_host.c",
    ROOT / "src" / "drivers" / "font.c",
)
RTC_RESULT_DIR = ROOT / "build" / "test-results" / "rtc-status-host"
RTC_BINARY = ROOT / "build" / "tests" / "test_rtc_status_host.exe"
RTC_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_rtc_host.c",
    ROOT / "src" / "drivers" / "rtc.c",
    ROOT / "src" / "core" / "string.c",
)
WIFI_MANAGER_RESULT_DIR = ROOT / "build" / "test-results" / "wifi-manager-host"
WIFI_MANAGER_BINARY = ROOT / "build" / "tests" / "test_wifi_manager_host.exe"
WIFI_MANAGER_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_wifi_manager_host.c",
    ROOT / "src" / "core" / "wifi_manager.c",
    ROOT / "src" / "core" / "string.c",
)
USB_MANAGER_RESULT_DIR = ROOT / "build" / "test-results" / "usb-manager-host"
USB_MANAGER_BINARY = ROOT / "build" / "tests" / "test_usb_manager_host.exe"
USB_MANAGER_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_usb_manager_host.c",
    ROOT / "src" / "core" / "usb_manager.c",
    ROOT / "src" / "core" / "string.c",
)
USB_HID_RESULT_DIR = ROOT / "build" / "test-results" / "usb-hid-host"
USB_HID_BINARY = ROOT / "build" / "tests" / "test_usb_hid_host.exe"
USB_HID_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_usb_hid_host.c",
    ROOT / "src" / "drivers" / "usb_hid.c",
    ROOT / "src" / "core" / "string.c",
)
USB_MSC_RESULT_DIR = ROOT / "build" / "test-results" / "usb-msc-host"
USB_MSC_BINARY = ROOT / "build" / "tests" / "test_usb_msc_host.exe"
USB_MSC_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_usb_msc_host.c",
    ROOT / "src" / "drivers" / "usb_msc.c",
    ROOT / "src" / "core" / "string.c",
)
DEVFS_RESULT_DIR = ROOT / "build" / "test-results" / "devfs-host"
DEVFS_BINARY = ROOT / "build" / "tests" / "test_devfs_host.exe"
DEVFS_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_devfs_host.c",
    ROOT / "src" / "fs" / "devfs.c",
    ROOT / "src" / "core" / "string.c",
)
PROCFS_RESULT_DIR = ROOT / "build" / "test-results" / "procfs-host"
PROCFS_BINARY = ROOT / "build" / "tests" / "test_procfs_host.exe"
PROCFS_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_procfs_host.c",
    ROOT / "src" / "fs" / "procfs.c",
    ROOT / "src" / "core" / "string.c",
)
WAV_RESULT_DIR = ROOT / "build" / "test-results" / "wav-host"
WAV_BINARY = ROOT / "build" / "tests" / "test_wav_host.exe"
WAV_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_wav_host.c",
    ROOT / "src" / "fs" / "wav.c",
    ROOT / "src" / "core" / "string.c",
)
BMP_RESULT_DIR = ROOT / "build" / "test-results" / "bmp-host"
BMP_BINARY = ROOT / "build" / "tests" / "test_bmp_host.exe"
BMP_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_bmp_host.c",
    ROOT / "src" / "fs" / "bmp.c",
    ROOT / "src" / "core" / "string.c",
)
RNG_RESULT_DIR = ROOT / "build" / "test-results" / "rng-host"
RNG_BINARY = ROOT / "build" / "tests" / "test_rng_host.exe"
RNG_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_rng_host.c",
    ROOT / "src" / "drivers" / "rng.c",
    ROOT / "src" / "core" / "string.c",
)
SERIAL_RESULT_DIR = ROOT / "build" / "test-results" / "serial-host"
SERIAL_BINARY = ROOT / "build" / "tests" / "test_serial_host.exe"
SERIAL_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_serial_host.c",
    ROOT / "src" / "drivers" / "serial.c",
)
TSS_RESULT_DIR = ROOT / "build" / "test-results" / "tss-host"
TSS_BINARY = ROOT / "build" / "tests" / "test_tss_host.exe"
TSS_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_tss_host.c",
    ROOT / "src" / "drivers" / "tss.c",
    ROOT / "src" / "core" / "string.c",
)
SPEAKER_RESULT_DIR = ROOT / "build" / "test-results" / "speaker-host"
SPEAKER_BINARY = ROOT / "build" / "tests" / "test_speaker_host.exe"
SPEAKER_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_speaker_host.c",
    ROOT / "src" / "drivers" / "speaker.c",
)
KEYBOARD_RESULT_DIR = ROOT / "build" / "test-results" / "keyboard-host"
KEYBOARD_BINARY = ROOT / "build" / "tests" / "test_keyboard_host.exe"
KEYBOARD_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_keyboard_host.c",
    ROOT / "src" / "drivers" / "keyboard.c",
)
PROTOCOL_ADAPTER_RESULT_DIR = ROOT / "build" / "test-results" / "protocol-adapter-host"
PROTOCOL_ADAPTER_BINARY = ROOT / "build" / "tests" / "test_protocol_host.exe"
PROTOCOL_ADAPTER_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_protocol_host.c",
    ROOT / "src" / "core" / "test_protocol.c",
    ROOT / "src" / "core" / "test_protocol_core.c",
)
SHELL_INPUT_RESULT_DIR = ROOT / "build" / "test-results" / "shell-input-host"
SHELL_INPUT_BINARY = ROOT / "build" / "tests" / "test_shell_input_host.exe"
SHELL_INPUT_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_shell_input_host.c",
    ROOT / "src" / "shell" / "shell_input.c",
    ROOT / "src" / "core" / "string.c",
)
SHELL_COMMAND_UTILS_RESULT_DIR = ROOT / "build" / "test-results" / "shell-command-utils-host"
SHELL_COMMAND_UTILS_BINARY = ROOT / "build" / "tests" / "test_shell_command_utils_host.exe"
SHELL_COMMAND_UTILS_SOURCE_FILES = (
    ROOT / "tests" / "unit" / "test_shell_command_utils_host.c",
    ROOT / "src" / "shell" / "shell_command_utils.c",
    ROOT / "src" / "core" / "string.c",
)
SOURCE_FILES = CORE_SOURCE_FILES


def case_configuration(case_id: str) -> tuple[Path, Path, tuple[Path, ...], str]:
    if case_id == "host:core:contracts":
        return RESULT_DIR, DEFAULT_BINARY, CORE_SOURCE_FILES, "core-host"
    if case_id == "host:core:net-buffer":
        return NETWORK_RESULT_DIR, NETWORK_BINARY, NETWORK_SOURCE_FILES, "network-host"
    if case_id == "host:core:network-manager":
        return (NETWORK_MANAGER_RESULT_DIR, NETWORK_MANAGER_BINARY,
                NETWORK_MANAGER_SOURCE_FILES, "network-manager-host")
    if case_id == "host:network:route":
        return ROUTE_RESULT_DIR, ROUTE_BINARY, ROUTE_SOURCE_FILES, "route-host"
    if case_id == "host:network:ipv4":
        return IPV4_RESULT_DIR, IPV4_BINARY, IPV4_SOURCE_FILES, "ipv4-host"
    if case_id == "host:core:crypto":
        return CRYPTO_RESULT_DIR, CRYPTO_BINARY, CRYPTO_SOURCE_FILES, "crypto-host"
    if case_id == "host:core:scheduling":
        return (SCHEDULING_RESULT_DIR, SCHEDULING_BINARY,
                SCHEDULING_SOURCE_FILES, "scheduling-host")
    if case_id == "host:core:app-package":
        return PACKAGE_RESULT_DIR, PACKAGE_BINARY, PACKAGE_SOURCE_FILES, "package-host"
    if case_id == "host:core:state":
        return STATE_RESULT_DIR, STATE_BINARY, STATE_SOURCE_FILES, "state-host"
    if case_id == "host:core:device-manager":
        return (DEVICE_RESULT_DIR, DEVICE_BINARY,
                DEVICE_SOURCE_FILES, "device-manager-host")
    if case_id == "host:core:app-api":
        return APP_API_RESULT_DIR, APP_API_BINARY, APP_API_SOURCE_FILES, "app-api-host"
    if case_id == "host:core:app-files":
        return APP_FILES_RESULT_DIR, APP_FILES_BINARY, APP_FILES_SOURCE_FILES, "app-files-host"
    if case_id == "host:core:app-builtin":
        return APP_BUILTIN_RESULT_DIR, APP_BUILTIN_BINARY, APP_BUILTIN_SOURCE_FILES, "app-builtin-host"
    if case_id == "host:core:app-catalog":
        return (APP_CATALOG_RESULT_DIR, APP_CATALOG_BINARY,
                APP_CATALOG_SOURCE_FILES, "app-catalog-host")
    if case_id == "host:core:input":
        return INPUT_RESULT_DIR, INPUT_BINARY, INPUT_SOURCE_FILES, "input-host"
    if case_id == "host:core:power":
        return POWER_RESULT_DIR, POWER_BINARY, POWER_SOURCE_FILES, "power-host"
    if case_id == "host:storage:vfs-path":
        return (VFS_PATH_RESULT_DIR, VFS_PATH_BINARY,
                VFS_PATH_SOURCE_FILES, "vfs-path-host")
    if case_id == "host:storage:file-index":
        return (FILE_INDEX_RESULT_DIR, FILE_INDEX_BINARY,
                FILE_INDEX_SOURCE_FILES, "file-index-host")
    if case_id == "host:storage:fs":
        return FS_RESULT_DIR, FS_BINARY, FS_SOURCE_FILES, "fs-host"
    if case_id == "host:storage:storage":
        return STORAGE_RESULT_DIR, STORAGE_BINARY, STORAGE_SOURCE_FILES, "storage-host"
    if case_id == "host:storage:block":
        return BLOCK_RESULT_DIR, BLOCK_BINARY, BLOCK_SOURCE_FILES, "block-host"
    if case_id == "host:storage:fat12":
        return FAT12_RESULT_DIR, FAT12_BINARY, FAT12_SOURCE_FILES, "fat12-host"
    if case_id == "host:storage:fat32":
        return FAT32_RESULT_DIR, FAT32_BINARY, FAT32_SOURCE_FILES, "fat32-host"
    if case_id == "host:storage:vfs":
        return VFS_RESULT_DIR, VFS_BINARY, VFS_SOURCE_FILES, "vfs-host"
    if case_id == "host:memory:slab-metadata":
        return SLAB_RESULT_DIR, SLAB_BINARY, SLAB_SOURCE_FILES, "slab-host"
    if case_id == "host:core:timer":
        return TIMER_RESULT_DIR, TIMER_BINARY, TIMER_SOURCE_FILES, "timer-host"
    if case_id == "host:network:udp":
        return UDP_RESULT_DIR, UDP_BINARY, UDP_SOURCE_FILES, "udp-host"
    if case_id == "host:network:arp":
        return ARP_RESULT_DIR, ARP_BINARY, ARP_SOURCE_FILES, "arp-host"
    if case_id == "host:network:icmp":
        return ICMP_RESULT_DIR, ICMP_BINARY, ICMP_SOURCE_FILES, "icmp-host"
    if case_id == "host:network:dns":
        return DNS_RESULT_DIR, DNS_BINARY, DNS_SOURCE_FILES, "dns-host"
    if case_id == "host:network:dhcp":
        return DHCP_RESULT_DIR, DHCP_BINARY, DHCP_SOURCE_FILES, "dhcp-host"
    if case_id == "host:network:ethernet":
        return (ETHERNET_RESULT_DIR, ETHERNET_BINARY,
                ETHERNET_SOURCE_FILES, "ethernet-host")
    if case_id == "host:network:tcp":
        return TCP_RESULT_DIR, TCP_BINARY, TCP_SOURCE_FILES, "tcp-host"
    if case_id == "host:security:tls":
        return TLS_RESULT_DIR, TLS_BINARY, TLS_SOURCE_FILES, "tls-host"
    if case_id == "host:network:http":
        return HTTP_RESULT_DIR, HTTP_BINARY, HTTP_SOURCE_FILES, "http-host"
    if case_id == "host:network:socket":
        return (NET_SOCKET_RESULT_DIR, NET_SOCKET_BINARY,
                NET_SOCKET_SOURCE_FILES, "net-socket-host")
    if case_id == "host:memory:vma":
        return VMA_RESULT_DIR, VMA_BINARY, VMA_SOURCE_FILES, "vma-host"
    if case_id == "host:memory:paging":
        return PAGING_RESULT_DIR, PAGING_BINARY, PAGING_SOURCE_FILES, "paging-host"
    if case_id == "host:memory:memory":
        return MEMORY_RESULT_DIR, MEMORY_BINARY, MEMORY_SOURCE_FILES, "memory-host"
    if case_id == "host:process:signals":
        return SIGNAL_RESULT_DIR, SIGNAL_BINARY, SIGNAL_SOURCE_FILES, "process-signal-host"
    if case_id == "host:process:ipc":
        return IPC_RESULT_DIR, IPC_BINARY, IPC_SOURCE_FILES, "process-ipc-host"
    if case_id == "host:core:workqueue":
        return WORKQUEUE_RESULT_DIR, WORKQUEUE_BINARY, WORKQUEUE_SOURCE_FILES, "workqueue-host"
    if case_id == "host:core:bearssl-compat":
        return (BEARSSL_COMPAT_RESULT_DIR, BEARSSL_COMPAT_BINARY,
                BEARSSL_COMPAT_SOURCE_FILES, "bearssl-compat-host")
    if case_id == "host:shell:dispatch":
        return (SHELL_DISPATCH_RESULT_DIR, SHELL_DISPATCH_BINARY,
                SHELL_DISPATCH_SOURCE_FILES, "shell-dispatch-host")
    if case_id == "host:shell:introspection":
        return (SHELL_INTROSPECTION_RESULT_DIR, SHELL_INTROSPECTION_BINARY,
                SHELL_INTROSPECTION_SOURCE_FILES, "shell-introspection-host")
    if case_id == "host:drivers:font":
        return FONT_RESULT_DIR, FONT_BINARY, FONT_SOURCE_FILES, "font-host"
    if case_id == "host:drivers:rtc-status":
        return RTC_RESULT_DIR, RTC_BINARY, RTC_SOURCE_FILES, "rtc-status-host"
    if case_id == "host:core:wifi-manager":
        return (WIFI_MANAGER_RESULT_DIR, WIFI_MANAGER_BINARY,
                WIFI_MANAGER_SOURCE_FILES, "wifi-manager-host")
    if case_id == "host:core:usb-manager":
        return (USB_MANAGER_RESULT_DIR, USB_MANAGER_BINARY,
                USB_MANAGER_SOURCE_FILES, "usb-manager-host")
    if case_id == "host:drivers:usb-hid":
        return USB_HID_RESULT_DIR, USB_HID_BINARY, USB_HID_SOURCE_FILES, "usb-hid-host"
    if case_id == "host:drivers:usb-msc":
        return USB_MSC_RESULT_DIR, USB_MSC_BINARY, USB_MSC_SOURCE_FILES, "usb-msc-host"
    if case_id == "host:storage:devfs":
        return DEVFS_RESULT_DIR, DEVFS_BINARY, DEVFS_SOURCE_FILES, "devfs-host"
    if case_id == "host:storage:procfs":
        return PROCFS_RESULT_DIR, PROCFS_BINARY, PROCFS_SOURCE_FILES, "procfs-host"
    if case_id == "host:storage:wav":
        return WAV_RESULT_DIR, WAV_BINARY, WAV_SOURCE_FILES, "wav-host"
    if case_id == "host:storage:bmp":
        return BMP_RESULT_DIR, BMP_BINARY, BMP_SOURCE_FILES, "bmp-host"
    if case_id == "host:drivers:rng":
        return RNG_RESULT_DIR, RNG_BINARY, RNG_SOURCE_FILES, "rng-host"
    if case_id == "host:drivers:serial":
        return SERIAL_RESULT_DIR, SERIAL_BINARY, SERIAL_SOURCE_FILES, "serial-host"
    if case_id == "host:drivers:tss":
        return TSS_RESULT_DIR, TSS_BINARY, TSS_SOURCE_FILES, "tss-host"
    if case_id == "host:drivers:speaker":
        return SPEAKER_RESULT_DIR, SPEAKER_BINARY, SPEAKER_SOURCE_FILES, "speaker-host"
    if case_id == "host:drivers:keyboard":
        return KEYBOARD_RESULT_DIR, KEYBOARD_BINARY, KEYBOARD_SOURCE_FILES, "keyboard-host"
    if case_id == "host:tst2:protocol-adapter":
        return (PROTOCOL_ADAPTER_RESULT_DIR, PROTOCOL_ADAPTER_BINARY,
                PROTOCOL_ADAPTER_SOURCE_FILES, "protocol-adapter-host")
    if case_id == "host:shell:input":
        return (SHELL_INPUT_RESULT_DIR, SHELL_INPUT_BINARY,
                SHELL_INPUT_SOURCE_FILES, "shell-input-host")
    if case_id == "host:shell:command-utils":
        return (SHELL_COMMAND_UTILS_RESULT_DIR, SHELL_COMMAND_UTILS_BINARY,
                SHELL_COMMAND_UTILS_SOURCE_FILES, "shell-command-utils-host")
    raise ValueError(f"caso_host_invalido:{case_id}")


def executable(value: str) -> str | None:
    candidate = value.strip().strip('"')
    if not candidate:
        return None
    path = Path(candidate)
    if path.is_file():
        return str(path)
    return shutil.which(candidate)


def command_environment(compiler: str) -> dict[str, str]:
    environment = os.environ.copy()
    parent = Path(compiler).parent
    if parent != Path('.'):
        environment["PATH"] = str(parent) + os.pathsep + environment.get("PATH", "")
    return environment


def run_process(command: list[str], environment: dict[str, str],
                timeout: float) -> dict[str, Any]:
    started = time.monotonic()
    try:
        completed = subprocess.run(command, cwd=ROOT, env=environment,
                                   capture_output=True, text=True,
                                   timeout=timeout, check=False)
        result = {
            "status": "PASS" if completed.returncode == 0 else "FAIL",
            "returncode": completed.returncode,
            "stdout": completed.stdout,
            "stderr": completed.stderr,
        }
    except subprocess.TimeoutExpired as error:
        result = {
            "status": "TIMEOUT",
            "returncode": None,
            "stdout": error.stdout or "",
            "stderr": error.stderr or "",
        }
    result["command"] = command
    result["duration_seconds"] = round(time.monotonic() - started, 6)
    return result


def compiler_command(compiler: str, binary: Path,
                     sources: tuple[Path, ...] | None = None) -> list[str]:
    selected_sources = sources or SOURCE_FILES
    compatibility_flags = []
    if any(source.name == "crypto_ed25519.c" for source in selected_sources):
        compatibility_flags.append("-Wno-unused-const-variable")
    if any(source.name == "bearssl_compat.c" for source in selected_sources):
        compatibility_flags.append("-fno-builtin")
    if any(source.name == "wav.c" for source in selected_sources):
        compatibility_flags.append("-fno-builtin")
    return [
        compiler, "-std=c11", "-O0", "-fno-inline", "-ffunction-sections",
        "-fdata-sections", "-Wall", "-Wextra",
        "-Werror", "-DZEPHYROS_HOST_TEST=1", "-finstrument-functions",
        *compatibility_flags,
        "-I", str(ROOT / "tests" / "unit" / "host_include"),
        "-I", str(ROOT / "src" / "include"),
        "-I", str(ROOT / "src" / "core"),
        *(str(source) for source in selected_sources), "-Wl,--gc-sections",
        "-o", str(binary),
    ]


def find_nm(compiler: str, environment: dict[str, str]) -> str | None:
    compiler_path = Path(compiler)
    candidates = [compiler_path.with_name("nm.exe"), compiler_path.with_name("nm")]
    candidates.extend([Path("nm.exe"), Path("nm")])
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)
        resolved = shutil.which(str(candidate), path=environment.get("PATH"))
        if resolved:
            return resolved
    return None


def symbol_map(binary: Path, nm: str, environment: dict[str, str],
               timeout: float) -> tuple[list[dict[str, Any]] | None, dict[str, Any]]:
    result = run_process([nm, "-n", str(binary)], environment, timeout)
    if result["status"] != "PASS":
        return None, result
    return coverage_collector.parse_nm(result["stdout"]), result


def attach_surface_ids(symbols: list[dict[str, Any]],
                       catalog: dict[str, Any],
                       sources: tuple[Path, ...] | None = None) -> list[dict[str, Any]]:
    selected_sources = sources or SOURCE_FILES
    source_names = {path.relative_to(ROOT).as_posix() for path in selected_sources
                    if path.as_posix().startswith((ROOT / "src").as_posix())}
    candidates: dict[str, list[str]] = {}
    for surface in catalog.get("surfaces", []):
        if not isinstance(surface, dict) or surface.get("kind") != "c_function":
            continue
        if surface.get("source") not in source_names:
            continue
        symbol = surface.get("symbol")
        identifier = surface.get("id")
        if isinstance(symbol, str) and isinstance(identifier, str):
            candidates.setdefault(symbol, []).append(identifier)
    enriched = []
    for symbol in symbols:
        item = dict(symbol)
        options = candidates.get(symbol.get("symbol"), [])
        if len(options) == 1:
            item["surface_id"] = options[0]
        enriched.append(item)
    return enriched


def write_artifacts(result_dir: Path, manifest: dict[str, Any], result: dict[str, Any],
                    coverage: dict[str, Any] | None,
                    symbols: list[dict[str, Any]] | None) -> None:
    result_dir.mkdir(parents=True, exist_ok=True)
    (result_dir / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    (result_dir / "result.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    (result_dir / "coverage.json").write_text(
        json.dumps(coverage or {}, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    (result_dir / "coverage-symbols.json").write_text(
        json.dumps({"schema": coverage_collector.SYMBOL_SCHEMA,
                    "symbols": symbols or []}, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    stdout = "\n".join(step.get("stdout", "") for step in result["steps"])
    stderr = "\n".join(step.get("stderr", "") for step in result["steps"])
    (result_dir / "stdout.log").write_text(stdout, encoding="utf-8")
    (result_dir / "stderr.log").write_text(stderr, encoding="utf-8")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("HOST_CC", "cc"))
    parser.add_argument("--binary", default=str(DEFAULT_BINARY))
    parser.add_argument("--case", default="host:core:contracts",
                        choices=("host:core:contracts", "host:core:net-buffer",
                                 "host:core:network-manager",
                                 "host:network:route", "host:network:ipv4",
                                 "host:core:crypto", "host:core:scheduling",
                                 "host:core:app-package", "host:core:state",
                                 "host:core:device-manager", "host:core:app-api",
                                 "host:core:app-files", "host:core:app-builtin",
                                 "host:core:app-catalog",
                                 "host:core:input",
                                 "host:core:power", "host:storage:vfs-path",
                                 "host:storage:file-index", "host:storage:fs",
                                 "host:storage:storage", "host:storage:block",
                                 "host:storage:fat12", "host:storage:fat32",
                                 "host:storage:vfs",
                                 "host:memory:slab-metadata",
                                 "host:core:timer", "host:network:udp",
                                 "host:network:arp", "host:network:icmp",
                                 "host:network:dns", "host:network:dhcp",
                                 "host:network:ethernet", "host:network:tcp",
                                 "host:security:tls", "host:network:http",
                                 "host:network:socket", "host:memory:vma",
                                 "host:memory:paging", "host:memory:memory",
                                 "host:process:signals", "host:process:ipc",
                                 "host:core:workqueue", "host:core:bearssl-compat",
                                 "host:shell:dispatch", "host:shell:introspection",
                                 "host:drivers:font", "host:drivers:rtc-status",
                                 "host:core:wifi-manager",
                                 "host:core:usb-manager",
                                 "host:drivers:usb-hid",
                                 "host:drivers:usb-msc",
                                 "host:storage:devfs",
                                 "host:storage:procfs",
                                 "host:storage:wav",
                                 "host:storage:bmp",
                                 "host:drivers:rng",
                                 "host:drivers:serial",
                                 "host:drivers:tss",
                                 "host:drivers:speaker",
                                 "host:drivers:keyboard",
                                 "host:tst2:protocol-adapter",
                                 "host:shell:input", "host:shell:command-utils"))
    parser.add_argument("--catalog", default=str(ROOT / "tests" / "catalog.json"))
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    result_dir, default_binary, source_files, suite = case_configuration(arguments.case)
    compiler = executable(arguments.cc)
    binary_value = arguments.binary
    if arguments.case != "host:core:contracts" and binary_value == str(DEFAULT_BINARY):
        binary_value = str(default_binary)
    binary = Path(binary_value)
    if not binary.is_absolute():
        binary = ROOT / binary
    manifest = {
        "suite": suite,
        "case_id": arguments.case,
        "started_at": datetime.now(timezone.utc).isoformat(),
        "compiler_requested": arguments.cc,
        "compiler": compiler,
        "timeout_seconds": arguments.timeout,
        "instrumentation": "-finstrument-functions",
        "sources": [str(path.relative_to(ROOT)) for path in source_files],
    }
    result: dict[str, Any] = {"suite": suite, "status": "PASS",
                              "steps": [], "cause": None}
    if arguments.timeout <= 0:
        result["status"] = "FAIL"
        result["cause"] = "timeout_invalido"
        write_artifacts(result_dir, manifest, result, None, None)
        return 1
    if not compiler:
        result["status"] = "BLOCKED"
        result["cause"] = f"host_compiler_unavailable:{arguments.cc}"
        write_artifacts(result_dir, manifest, result, None, None)
        print(f"Core host: BLOCKED {result['cause']}", file=sys.stderr)
        return 2

    environment = command_environment(compiler)
    catalog = coverage_collector.read_json(Path(arguments.catalog))
    binary.parent.mkdir(parents=True, exist_ok=True)
    compile_result = run_process(compiler_command(compiler, binary, source_files), environment,
                                 arguments.timeout)
    result["steps"].append(compile_result)
    if compile_result["status"] != "PASS":
        result["status"] = compile_result["status"]
        result["cause"] = "core_host_compile"
        write_artifacts(result_dir, manifest, result, None, None)
        return 1

    run_result = run_process([str(binary)], environment, arguments.timeout)
    result["steps"].append(run_result)
    nm = find_nm(compiler, environment)
    if not nm:
        result["status"] = "BLOCKED"
        result["cause"] = "nm_unavailable_for_coverage"
        write_artifacts(result_dir, manifest, result, None, None)
        return 2
    symbols, nm_result = symbol_map(binary, nm, environment, arguments.timeout)
    result["steps"].append(nm_result)
    coverage = None
    if symbols is not None and run_result["stdout"]:
        try:
            symbols = attach_surface_ids(symbols, catalog, source_files)
            coverage = coverage_collector.collect_report(
                run_result["stdout"], symbols)
        except (coverage_collector.CoverageError, OSError) as error:
            result["cause"] = f"coverage_collection:{error}"
    if run_result["status"] != "PASS":
        result["status"] = run_result["status"]
        result["cause"] = result["cause"] or "core_host_test"
    elif coverage is None or coverage["status"] != "PASS":
        result["status"] = "FAIL"
        result["cause"] = result["cause"] or "coverage_incomplete"
    write_artifacts(result_dir, manifest, result, coverage, symbols)
    print(f"Core host: {result['status']}")
    return 0 if result["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
