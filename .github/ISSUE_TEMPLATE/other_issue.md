---
name: Other issue
about: Minor issues that are not bugs or features, but should be addressed
title: ''
labels: 'help wanted'
assignees: ''

---

**GitHub is for bugs and features - not for support**
For support, please use the hashcat forums https://hashcat.net/forum/

**Check the FAQ**
Some items that might appear to be issues are not issues. Please review the hashcat FAQ https://hashcat.net/wiki/doku.php?id=frequently_asked_questions before submitting a bug report.

**Describe the issue**
A clear and concise description of what the issue is.

**To Reproduce**
Please provide us with all files required to reproduce the issue locally on our development systems. For instance: hash files, wordlists, rule files, ...

**Expected behavior**
A clear and concise description of what you expected to happen.

**Automatic shooter_hashcat error report**
If the console printed `Error report saved to:`, attach that `.log` file after
reviewing it for private paths, arguments, hashes, rules, or quoted input.
`--brain-password` values are redacted automatically. See the
[error-report guide](https://github.com/Shooter3k/shooter_hashcat/blob/master/docs/error-reports.md)
for details. Do not attach an unreviewed report.

**Hardware/Compute device (please complete the following information):**
- Compute device name: [e.g. RTX2080Ti]
- OpenCL/CUDA driver name: [e.g. NVIDIA DRIVER]
- OpenCL/CUDA driver version: [e.g. 580.10]
- OpenCL/CUDA driver source: [e.g. runtime installer/.exe installer]

**Hashcat version (please complete the following information):**
 - OS: [e.g. Linux]
 - Distribution: [e.g. Ubuntu 24.04]
 - Version: [e.g. 7.1.2]

**Diagnostic output compute devices:**


```
For NV: Post nvidia-smi output. This tool also exist on Windows
For AMD ROCm: Post rocm-smi and rocminfo output
```

```
Post clinfo output
```

```
Post hashcat -I output
```

```
On Linux: post lspci output
```

**Additional context**
Add any other context about the problem here. For instance, it was working with hashcat version X.X.X (also please post output from older versions).
