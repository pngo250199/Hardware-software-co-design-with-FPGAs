# Hardware-software-co-design-with-FPGAs

## 1. Why FPGA and What Makes It Different from an MCU

The Zynq-7000 was selected because it combines an ARM Cortex-A9 processing system with FPGA programmable logic on the same device. Compared with a traditional microcontroller, such as the STM32, the Zynq platform provides significantly more processing resources and allows both software and custom hardware logic to operate together.

A microcontroller mainly executes instructions sequentially on its CPU core and is well suited for real-time control tasks such as reading sensors, generating PWM signals, and controlling motors. In contrast, an FPGA allows developers to create custom hardware IP blocks that can perform operations in parallel. These hardware blocks can run independently from the ARM software, which makes the FPGA useful for accelerating computationally intensive workloads.

The ARM Cortex-A9 is used for software control, system coordination, and communication, while the FPGA programmable logic is used for hardware acceleration. This architecture is especially useful for workloads such as image processing, video pipelines, filtering, and edge detection.

The Zynq platform also provides room for future expansion. Custom FPGA resources can be reused to build more advanced accelerators, including image-processing pipelines, digital signal-processing blocks, and machine-learning inference hardware. This makes the platform valuable for learning hardware/software co-design and for developing reusable embedded-system architectures.

## 2. FPGA Fundamentals

An FPGA (Field-Programmable Gate Array) is a programmable digital hardware device that allows developers to create custom logic circuits instead of only executing software instructions on a processor. FPGA designs are typically described using a hardware description language such as Verilog or VHDL and are synthesized into physical FPGA resources.

The main FPGA resources used in digital design include:

- **LUTs (Look-Up Tables):** Implement combinational logic such as Boolean equations, multiplexers, and arithmetic operations.
- **Flip-Flops / Registers:** Store data between clock cycles and are used to build sequential logic, pipelines, counters, and state machines.
- **BRAM (Block RAM):** Dedicated on-chip memory used for buffers, lookup tables, frame data, and temporary storage.
- **DSP Slices:** Dedicated arithmetic hardware optimized for operations such as multiplication, addition, accumulation, and signal-processing calculations.
- **Clocking Resources:** Distribute clock signals throughout the FPGA and synchronize sequential hardware logic.

Unlike a CPU or MCU, where instructions are executed by a processor, FPGA logic can operate concurrently. Multiple hardware modules can process data during the same clock cycle. This parallel execution is one of the main reasons FPGAs are useful for workloads such as image processing, digital signal processing, and hardware acceleration.

The FPGA development flow used in this project follows:

`RTL Design → Synthesis → Implementation → Bitstream Generation → Hardware Programming`

During synthesis, the Verilog RTL is converted into FPGA resources such as LUTs and flip-flops. During implementation, Vivado performs placement and routing to physically map the design onto the FPGA device. The generated bitstream is then programmed into the FPGA to configure the hardware.

For the Zynq-7000 platform, the FPGA programmable logic is also connected to the ARM Cortex-A9 processing system through AXI interfaces. This allows software running on the processor to configure custom FPGA IP blocks, send data to the hardware, and read results back through memory-mapped registers.
