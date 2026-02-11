
# runec – Run with EtherCAT Capabilities

A minimal setuid helper that grants network capabilities (`CAP_NET_RAW`, `CAP_NET_ADMIN`) to unprivileged processes, enabling userspace EtherCAT master stacks and other raw-socket applications to run without root.

## The Problem

EtherCAT master stacks (such as [SOEM](https://github.com/OpenEtherCATsociety/SOEM) and [IgH EtherCAT Master](https://etherlab.org/en/ethercat/)) need to:

- Open raw sockets (`AF_PACKET`) to send and receive Ethernet frames directly
- Set network interfaces to promiscuous mode
- Configure interface flags

On Linux, these operations require either:

1. Running as **root** — a significant security risk for industrial control applications
2. Granting **file capabilities** to every binary — tedious, breaks on every recompile, doesn't propagate through wrapper scripts, and doesn't work with interpreted languages or dynamically linked loaders
3. Running inside a **Docker container** with `--cap-add` — adds deployment complexity

None of these options are great for a development workflow where you're recompiling and running a control application dozens of times per hour.

## The Solution

`runec` is a tiny (~200 LOC) setuid-root C program that:

1. Starts with elevated privileges (setuid-root)
2. Immediately drops back to your real user identity
3. Retains **only** `CAP_NET_RAW` and `CAP_NET_ADMIN` through the Linux ambient capability mechanism
4. Executes your target program, which inherits just those two capabilities

The result: your application runs as your normal user, with your normal file permissions, but with the ability to open raw sockets and configure interfaces.

```
$ whoami
developer

$ runec ./my_ethercat_app eth0
[runec] Launching target...
EtherCAT master initialized on eth0
...
```

No root shell. No sudo. No capability annotations on every build artifact.

## How It Works

Understanding the Linux capability model helps explain why this tool exists and why it's implemented the way it is.

### Linux Capabilities in 60 Seconds

Linux splits the monolithic root privilege into ~40 individual **capabilities**. Each process has three capability sets:

| Set | Purpose |
|---|---|
| **Permitted** | Upper bound — what the process *could* use |
| **Effective** | What the kernel actually checks right now |
| **Inheritable** | What *may* survive across `execve()` |

There is also a fourth set, **Ambient** (since kernel 4.3), which is the key to making this work. Ambient capabilities are automatically granted to the new program after `execve()`, without requiring file capabilities on the target binary.

### What runec Does, Step by Step

```
┌────────────────────────────────────────────────────┐
│ 1. runec starts as setuid-root                     │
│    → euid=0, full capability sets                  │
├────────────────────────────────────────────────────┤
│ 2. prctl(PR_SET_KEEPCAPS, 1)                       │
│    → tells kernel to keep permitted caps            │
│      when we drop uid                              │
├────────────────────────────────────────────────────┤
│ 3. setresuid(real_uid, real_uid, real_uid)          │
│    → drop to original user                         │
│    → effective set is cleared                       │
│    → permitted set is preserved (KEEPCAPS)          │
├────────────────────────────────────────────────────┤
│ 4. cap_set_proc()                                  │
│    → restore CAP_NET_RAW + CAP_NET_ADMIN into      │
│      permitted, effective, and inheritable sets     │
├────────────────────────────────────────────────────┤
│ 5. prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE,...) │
│    → set both caps in the ambient set               │
│    → this is what survives across execve()          │
├────────────────────────────────────────────────────┤
│ 6. execv(target, args)                             │
│    → target runs as your user                       │
│    → target has CAP_NET_RAW + CAP_NET_ADMIN         │
│    → no other elevated privileges                   │
└────────────────────────────────────────────────────┘
```

### Why Ambient Capabilities?

Before kernel 4.3, there was no clean way to pass capabilities through `execve()` to an ordinary (no file capabilities, no setuid) binary. The inheritable set alone isn't enough — the new program must also have matching file inheritable capabilities, which brings us back to the `setcap`-on-every-binary problem.

Ambient capabilities solve this: if a capability is in both the permitted and inheritable sets, and is raised in the ambient set, it will appear in the permitted and effective sets of the executed program — regardless of that program's file capabilities.

## Requirements

- **Linux kernel ≥ 4.3** (for ambient capability support)
- **libcap** development headers (`libcap-dev` on Debian/Ubuntu, `libcap-devel` on Fedora/RHEL)
- **gcc** (or any C99 compiler)
- **sudo** access for installation (one-time)

### Install Dependencies

**Debian / Ubuntu:**
```bash
sudo apt install build-essential libcap-dev
```

**Fedora / RHEL / CentOS:**
```bash
sudo dnf install gcc make libcap-devel
```

**Arch Linux:**
```bash
sudo pacman -S base-devel libcap
```

## Building

```bash
# Clone the repository
git clone https://github.com/yourname/runec.git
cd runec

# Build with default settings (both capabilities, no debug logging)
make

# Build with debug logging to troubleshoot capability issues
make debug

# Build with only CAP_NET_RAW (no CAP_NET_ADMIN)
make ENABLE_CAP_NET_ADMIN=0
```

### Build Options

| Option | Default | Description |
|---|---|---|
| `ENABLE_CAP_NET_RAW` | `1` | Grant `CAP_NET_RAW` (raw sockets) |
| `ENABLE_CAP_NET_ADMIN` | `1` | Grant `CAP_NET_ADMIN` (promiscuous mode, interface config) |
| `ENABLE_DEBUG_LOG` | `0` | Print detailed capability state at each step |
| `PREFIX` | `/usr/local` | Installation prefix |

Options are passed as make variables:

```bash
make ENABLE_CAP_NET_RAW=1 ENABLE_CAP_NET_ADMIN=0 ENABLE_DEBUG_LOG=1
```

## Installation

```bash
# Build and install (installs to /usr/local/bin with setuid-root)
make
sudo make install
```

This performs the following:

1. Compiles `runec` with your chosen options
2. Installs the binary to `/usr/local/bin/runec` owned by root with mode `4755` (setuid)
3. Optionally sets file capabilities as a fallback (`cap_net_raw,cap_net_admin+ep`)
4. Verifies the installation

To install to a different location:

```bash
sudo make PREFIX=/opt/local install
```

### Uninstallation

```bash
sudo make uninstall
```

## Usage

```bash
runec <executable> [args...]
```

### Examples

**Run an EtherCAT application:**
```bash
runec ./build/bin/my_ethercat_app eth0
```

**Run a SOEM-based program:**
```bash
runec ./slaveinfo enp2s0
```

**Use in a CMake workflow:**
```bash
cmake --build build/
runec build/bin/ethercat_controller --interface enp2s0 --config motion.yaml
```

**Use with a shell wrapper:**
```bash
# runec propagates capabilities through execve,
# so this works even through scripts (the final binary receives the caps)
runec /bin/bash -c './my_app eth0'
```

### Integration with IDEs and Build Systems

**VS Code launch.json:**
```json
{
    "name": "EtherCAT App",
    "type": "cppdbg",
    "request": "launch",
    "program": "/usr/local/bin/runec",
    "args": ["${workspaceFolder}/build/bin/my_app", "enp2s0"],
    "cwd": "${workspaceFolder}"
}
```

**CMake custom target:**
```cmake
add_custom_target(run
    COMMAND runec $<TARGET_FILE:my_app> enp2s0
    DEPENDS my_app
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running with EtherCAT capabilities"
)
```

**Makefile integration:**
```makefile
run: build/bin/my_app
	runec $< enp2s0
```

## Security Considerations

### What runec Does

- Grants exactly two capabilities: `CAP_NET_RAW` and `CAP_NET_ADMIN` (configurable at compile time)
- Drops all other root privileges immediately
- Runs the target process as your normal user (your UID, your GID)
- Validates that the target is a regular file and is executable before launching
- Verifies capabilities are correctly set before calling `execve()`

### What runec Does NOT Do

- Does not grant root access
- Does not keep any capabilities beyond the two configured ones
- Does not modify any system configuration
- Does not bypass file permissions (the target runs as your user)
- Does not allow specifying arbitrary capabilities (they are fixed at compile time)

### Attack Surface

`runec` is a setuid-root binary, which inherently carries risk. The attack surface is minimized by:

1. **No user-controlled capability selection** — the capabilities are compiled in, not passed as arguments
2. **Immediate privilege drop** — root uid is dropped before any user-controlled data (the target path) is acted upon
3. **No environment manipulation** — the environment is passed through unmodified (the target is responsible for its own environment hygiene)
4. **Minimal code** — under 200 lines of C, straightforward to audit
5. **Target validation** — `stat()` check before `execve()` to verify the target exists and is a regular file

### Comparison to Alternatives

| Approach | Capabilities | Persists Across Recompile | Security Scope | Complexity |
|---|---|---|---|---|
| `sudo ./app` | All of root | ✅ | ❌ Full root | Low |
| `setcap` on binary | Selected | ❌ Must re-apply | ✅ Minimal | Medium |
| Docker `--cap-add` | Selected | ✅ | ✅ Contained | High |
| **runec** | Selected | ✅ | ✅ Minimal | **Low** |

## Troubleshooting

### "Your kernel may not support ambient capabilities"

Your kernel is older than 4.3. Check with:
```bash
uname -r
```

### "cap_set_proc failed"

The `runec` binary may not have setuid-root or file capabilities. Verify:
```bash
ls -l /usr/local/bin/runec
# Should show: -rwsr-xr-x 1 root root ... runec

getcap /usr/local/bin/runec
# Should show: /usr/local/bin/runec cap_net_admin,cap_net_raw=ep
```

Reinstall if needed:
```bash
sudo make install
```

### "Permission denied" on the target

`runec` runs the target as your normal user. Make sure you have execute permission:
```bash
chmod +x ./my_app
```

### Debug Logging

Rebuild with debug logging to see exactly what's happening:
```bash
make clean
make debug
sudo make install

runec ./my_app eth0
# Will print detailed capability state at each step
```

Example debug output:
```
[runec] Starting: euid=0, ruid=1000, egid=0, rgid=1000
[runec] After cap_set_proc: = cap_net_admin,cap_net_raw+eip
[runec] Final caps before exec: = cap_net_admin,cap_net_raw+eip
[runec] Launching: ./my_app
```

### Testing Without an EtherCAT Application

Verify that capabilities are passed through correctly:
```bash
# Install the test (if available)
make test

# Or manually check:
runec /bin/bash -c 'cat /proc/self/status | grep -i cap'
```

## Project Structure

```
runec/
├── runec.c       # Source — the entire program
├── Makefile      # Build system
├── README.md     # This file
└── LICENSE       # License
```

## License

[MIT](LICENSE)

## Contributing

This is intentionally a tiny project. If you find a bug or have a security concern, please open an issue. Pull requests are welcome for:

- Bug fixes
- Documentation improvements
- Support for additional capabilities (compile-time gated)
- Portability improvements

Please keep the codebase minimal and auditable — the entire point is that this is small enough to trust.