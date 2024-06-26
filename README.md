# SRP199

## Overview

With the diminishing returns of Moore’s law, many applications turn to hardware accelerators for performance gains (e.g., ChatGPT running on GPU farms instead of CPU farms). There are various types of hardware accelerators, including GPUs, FPGAs, and ASICs. For this research project, we will use FPGAs due to their flexibility. However, a significant challenge with FPGAs is the overhead of moving data, as the data often originates from the CPU's DRAM. For many applications, data access can negate the performance benefits of using an FPGA. This project aims to minimize data transfer overhead for applications with a streaming access pattern (sequential access).

## Minimizing Data Transfer Overhead in FPGAs

This project focuses on minimizing data transfer overhead in FPGA-based systems. The project is divided into two main parts:

1. Blowfish Encryption
2. Image Histogram Equalization

For each part, we conduct tests using the following modes:
- **InMemOrder**: Testing in-memory order.
- **HostedInOrder**: Testing hosted in-memory order.
- **OutMemOrder**: Testing out-of-memory order.
- **HostedOutOrder**: Testing hosted out-of-memory order.
- **UnOptimized**: Testing in-memory order without hls kernel optimization.
- **HMLib**: Efficient Data Streaming Transfer for HLS using Host Memory.


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
|   |-- HostedOutOrder/
|   |   |-- inputs/
|   |   |-- results/
|   |   `-- src/
|   |-- UnOptimized/
|   |   |-- inputs/
|   |   |-- results/
|   |   `-- src/
|   `-- HMLib/
|       |-- inputs/
|   |   |-- results/
|   |   `-- src/
|
`-- image_histogram_equalization/
    |-- InMemOrder/
    |   |-- inputs/
|   |   |-- results/
|   |   `-- src/
    |-- HostedOutOrder/
    |   |-- inputs/
|   |   |-- results/
|   |   `-- src/
    |-- OutMemOrder/
    |   |-- inputs/
|   |   |-- results/
|   |   `-- src/
|   |-- UnOptimized/
|   |   |-- inputs/
|   |   |-- results/
|   |   `-- src/
    `-- HMLib/
        |-- inputs/
        |-- results/
        `-- src/
```


## Tools and Platform

- **Synthesis Tool**: Vitis HLS
- **FPGA**: Alveo U250

## Acknowledgments

I would like to acknowledge the following project that was instrumental in the development of this repository:

- **Blowfish Implementation by Prophet6250**  
    This repository provided a foundational implementation of the Blowfish algorithm, which was crucial for my project's cryptographic functionality.  s
    Repository: [prophet6250/blowfish-implementation](https://github.com/prophet6250/blowfish-implementation)  
    License: MIT

- **Histogram Equalization Specification Demo by DavidLee528**  
    This repository offered a comprehensive implementation of histogram equalization and specification techniques, essential for the image processing aspects of my project.  
    Repository: [DavidLee528/Histogram-Equalization-Specification-Demo](https://github.com/DavidLee528/Histogram-Equalization-Specification-Demo)  
    License: Unspecified

I am grateful for the efforts of the original authors and contributors. Their work made my project possible.