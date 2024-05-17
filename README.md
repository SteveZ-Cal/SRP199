# SRP199
Minimizing Data Transfer Overhead in FPGAs

This project focuses on minimizing data transfer overhead in FPGA-based systems. The project is divided into two main parts:

1. Blowfish Encryption
2. Image Histogram Equalization

For each part, we conduct tests using the following modes:
- InMemOrder
- HostedInOrder
- OutOfMemOrder
- KernelFlex

## Table of Contents

```bash
|-- blowfish_encryption/
|   |-- InMemOrder/
|   |   |-- inputs/
|   |   |-- results/
|   |   `-- src/
|   |-- HostedInOrder/
|   |   |-- inputs/
|   |   |-- results/
|   |   `-- src/
|   |-- OutOfMemOrder/
|   |   |-- inputs/
|   |   |-- results/
|   |   `-- src/
|   `-- KernelFlex/
|       |-- inputs/
|   |   |-- results/
|   |   `-- src/
|
`-- image_histogram_equalization/
    |-- InMemOrder/
    |   |-- inputs/
|   |   |-- results/
|   |   `-- src/
    |-- HostedInOrder/
    |   |-- inputs/
|   |   |-- results/
|   |   `-- src/
    |-- OutOfMemOrder/
    |   |-- inputs/
|   |   |-- results/
|   |   `-- src/
    `-- KernelFlex/
        |-- inputs/
        |-- results/
        `-- src/
```