# Changelog

All notable changes to this project will be documented in this file.

## [1.0.1] - 2026-05-08

### Added
- GitHub Actions CI/CD workflow with automatic builds
- Multi-architecture support for releases (amd64 and arm64)
- Automated releases on tag push
- Capability validation tests in CI
- CHANGELOG.md

### Changed
- License changed from MIT to CC0 1.0 Universal
- Binary naming in releases to `runec_linux_amd64` and `runec_linux_arm64`

## [1.0.0] - 2026-05-08

### Initial Release
- Initial release of runec - setuid helper for capabilities
- Support for CAP_NET_RAW, CAP_NET_ADMIN, and CAP_SYS_NICE
- Minimal C implementation (~200 LOC)
- Build system with Makefile
