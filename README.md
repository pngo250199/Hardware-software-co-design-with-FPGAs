# Hardware-software-co-design-with-FPGAs

## 1. Why FPGA and What Makes It Different from an MCU

The Zynq-7000 was selected because it combines an ARM Cortex-A9 processing system with FPGA programmable logic on the same device. Compared with a traditional microcontroller, such as the STM32, the Zynq platform provides significantly more processing resources and allows both software and custom hardware logic to operate together.

A microcontroller mainly executes instructions sequentially on its CPU core and is well suited for real-time control tasks such as reading sensors, generating PWM signals, and controlling motors. In contrast, an FPGA allows developers to create custom hardware IP blocks that can perform operations in parallel. These hardware blocks can run independently from the ARM software, which makes the FPGA useful for accelerating computationally intensive workloads.

The ARM Cortex-A9 is used for software control, system coordination, and communication, while the FPGA programmable logic is used for hardware acceleration. This architecture is especially useful for workloads such as image processing, video pipelines, filtering, and edge detection.

The Zynq platform also provides room for future expansion. Custom FPGA resources can be reused to build more advanced accelerators, including image-processing pipelines, digital signal-processing blocks, and machine-learning inference hardware. This makes the platform valuable for learning hardware/software co-design and for developing reusable embedded-system architectures.

## 2. FPGA Fundamentals

An FPGA (Field-Programmable Gate Array) is a programmable digital hardware device that allows developers to create custom logic circuits instead of only executing software instructions on a processor. FPGA designs are commonly described using hardware description languages such as Verilog or VHDL and are synthesized into physical FPGA resources.

The main FPGA resources used in digital design include:

- **LUTs (Look-Up Tables):** Implement combinational logic, Boolean functions, multiplexers, and simple arithmetic.
- **Flip-Flops / Registers:** Store values between clock cycles and are used for sequential logic, pipelines, counters, and state machines.
- **BRAM (Block RAM):** Dedicated on-chip memory used for buffers, lookup tables, and temporary data storage.
- **DSP Slices:** Dedicated arithmetic hardware optimized for multiplication, addition, accumulation, and signal-processing operations.
- **Clocking Resources:** Distribute and synchronize clock signals throughout the FPGA design.

Unlike a CPU or MCU, which generally executes instructions sequentially, multiple FPGA hardware blocks can operate concurrently. This parallelism makes FPGA devices useful for workloads such as image processing, digital signal processing, hardware acceleration, and real-time data pipelines.

### FPGA Development Flow

The development flow used in this project is:

`RTL Design → Synthesis → Implementation → Bitstream Generation → FPGA Programming`

During synthesis, Verilog RTL is translated into FPGA resources such as LUTs and flip-flops. During implementation, Vivado performs placement and routing to map those resources onto the physical FPGA device. The generated bitstream is then programmed into the FPGA.

**Vivado Block Design**

![Vivado Block Design](docs/images/vivado_block_design.png)

The block design above shows the Zynq Processing System connected to the custom FPGA IP through an AXI interconnect. This allows the ARM Cortex-A9 processor to communicate with programmable logic through memory-mapped registers.

### Custom FPGA IP

Custom hardware IP is implemented in Verilog and connected to the ARM processing system through AXI4-Lite.

![Custom AXI IP RTL](docs/images/custom_ip_rtl.png)

For the first hardware/software co-design test, the custom IP exposes registers that allow the Cortex-A9 to read physical switch inputs and control FPGA-connected LEDs.

The communication path is:

`Cortex-A9 Software → AXI4-Lite → Custom FPGA IP → FPGA GPIO`

### AXI Memory-Mapped Communication

The custom peripheral uses memory-mapped registers so that software can access FPGA hardware using normal memory addresses.

Example register map:

| Offset | Function |
| --- | --- |
| `0x00` | Switch / Input Register |
| `0x04` | LED / Output Register |

The Cortex-A9 can access these registers using C pointers:

```c
value = *(volatile uint32_t *)(XPAR_CUSTOMIP_0_BASEADDR + 0x00U);

*(volatile uint32_t *)(XPAR_CUSTOMIP_0_BASEADDR + 0x04U) = value;
```

This provides a simple demonstration of hardware/software co-design because software running on the ARM processor directly communicates with custom digital hardware implemented inside the FPGA.

### FPGA Resource Utilization

After synthesis and implementation, Vivado provides reports showing how many FPGA resources are used by the design.

![FPGA Resource Utilization](docs/images/fpga_utilization.png)

These reports are useful for understanding the hardware cost of a design and will become increasingly important as image-processing and hardware-acceleration modules are added.

### Hardware Validation

The custom AXI peripheral was validated on the physical Zybo Z7 board.

![Zybo Hardware Test](docs/images/zybo_axi_test.jpg)

The ARM Cortex-A9 writes to the FPGA register through AXI, and the programmable logic drives the physical LEDs. This confirms the complete communication path between software and custom FPGA hardware.

### Future FPGA Development

The current AXI and GPIO implementation establishes the foundation for more computationally intensive FPGA modules. Future development will reuse the same hardware/software co-design architecture for:

- Grayscale image conversion
- Sobel and other edge-detection algorithms
- Parallel pixel processing
- Camera and video pipelines
- Hardware acceleration performance comparison
- Custom control and status registers
- Machine-learning inference accelerators
