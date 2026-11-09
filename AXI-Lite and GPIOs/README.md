## FPGA Development Progression

The FPGA portion of this project was developed progressively, starting with basic processor-to-FPGA communication and then moving toward high-bandwidth video processing. Each stage establishes a hardware or software concept that will later be reused in the autonomous explorer system.

---

## 1. AXI4-Lite: ARM-to-FPGA Communication

The first milestone was establishing communication between the ARM Cortex-A9 processing system and custom logic inside the FPGA.

A custom AXI4-Lite peripheral was created in Vivado and connected to the Zynq Processing System. The peripheral exposes memory-mapped registers that can be accessed directly from software running on the Cortex-A9.

The initial hardware test used the Zybo switches and LEDs:
```
`Switches -> Custom FPGA IP -> AXI4-Lite -> Cortex-A9 -> AXI4-Lite -> Custom FPGA IP -> LEDs`
```
The custom peripheral uses two main registers:

| Offset | Purpose                 |
| ------ | ----------------------- |
| `0x00` | Read switch/input state |
| `0x04` | Write LED/output state  |

The first register returns the physical switch values, while the second register drives the FPGA LED outputs.

```verilog
assign gpio_out = slv_reg1[3:0];
```

The Cortex-A9 accesses these FPGA registers using memory-mapped C pointers:

```c
value = *(volatile uint32_t *)(XPAR_CUSTOMIP_0_BASEADDR + 0x00U);

*(volatile uint32_t *)(XPAR_CUSTOMIP_0_BASEADDR + 0x04U) = value;
```

This allows the physical LEDs to mirror the switch state. The original lab similarly maps the switch input to the first register and LED control to the second register. 

### Why This Matters

The LED test is intentionally simple. Its purpose is to verify the complete hardware/software communication path:
```
`Cortex-A9 Software -> AXI4-Lite -> Custom FPGA IP -> Physical Hardware`
```
This same AXI control structure can later be reused to configure image-processing accelerators, set threshold values, start hardware operations, and read accelerator status.

**Recommended screenshot:** Vivado block diagram showing the Zynq Processing System, AXI interconnect, and custom IP.

---

## 2. AXI4-Stream and HDMI Output

After validating processor-to-FPGA control, the next stage introduced high-bandwidth video data.

AXI4-Lite is useful for control registers, but it is not designed for continuously transferring large amounts of pixel data. For video applications, AXI4-Stream is used instead.

The first video pipeline generated HDMI output from data stored in DDR memory:
```
`DDR Framebuffer -> VDMA -> AXI4-Stream -> Video Output -> HDMI`
```
The Video DMA (VDMA) transfers framebuffer data from DDR memory into the video stream. The Cortex-A9 configures the VDMA through AXI4-Lite, while the actual pixel data is transferred through AXI4-Stream.

This introduced several important FPGA video-processing components:

- Video DMA
- AXI4-Stream
- Framebuffers
- Video Timing Controller
- Dynamic pixel clocks
- RGB-to-HDMI conversion

The HDMI lab used an MM2S, or **Memory-Mapped to Stream**, VDMA channel to convert framebuffer data stored in DDR into a continuous video stream.
```
`DDR -> VDMA MM2S -> AXI4-Stream -> HDMI`
```
The output pipeline also converts 32-bit framebuffer data into 24-bit RGB video data before sending it to the display.

### Key Concept

This stage introduced the difference between the **control path** and the **data path**:
```
`Cortex-A9 -> AXI4-Lite -> Hardware Configuration`
```
while
```
`DDR -> AXI4-Stream -> Pixel Data`
```
**Recommended screenshot:** Vivado HDMI output block design.

---

## 3. HDMI Input and Output

The video architecture was then expanded from output-only into a complete input/output pipeline.

Instead of only reading framebuffer data, the system can receive external video through HDMI RX, write frames into DDR, and later stream them back to HDMI TX.

The architecture becomes:
```
`HDMI RX -> HDMI Decoder -> VDMA -> DDR -> VDMA -> HDMI TX`
```
Two VDMA directions are used.

### Stream-to-Memory-Mapped

S2MM transfers incoming AXI4-Stream video into DDR memory:
```
`HDMI Input -> AXI4-Stream -> VDMA S2MM -> DDR`
```
### Memory-Mapped-to-Stream

MM2S performs the reverse operation:
```
`DDR -> VDMA MM2S -> AXI4-Stream -> HDMI Output`
```
Both channels operating together allow frames to be captured, stored, selected, and displayed.

Multiple framebuffers also make it possible to separate the currently captured frame from the frame being displayed.

For example:
```
`Camera -> Framebuffer 0`
```
while simultaneously:
```
`Framebuffer 1 -> Display`
```
This architecture is important because future FPGA image-processing logic can operate on stored or streaming image data without requiring the processor to manipulate every pixel.

### Clock Domain Crossing

The HDMI decoder produces its own pixel clock, while the AXI4-Stream pipeline uses a different clock.

FIFO buffering is therefore used to safely move pixel data between clock domains.

This introduced another important FPGA design concept:

**Different hardware modules may operate at different clock frequencies, so data must be synchronized when crossing between clock domains.**

**Recommended screenshot:** HDMI RX -> VDMA -> DDR -> HDMI TX Vivado block design.

---

## 4. HDMI Input and VGA Output

The next stage reused the video-input architecture while changing the physical output interface.

The Zybo receives video through HDMI and outputs the processed video through the Pmod VGA interface.

The pipeline is approximately:
```
`HDMI Input -> HDMI Decoder -> FIFO -> VDMA -> DDR -> AXI4-Stream -> VGA`
```
The VGA interface requires:

- Red pixel data
- Green pixel data
- Blue pixel data
- Horizontal synchronization
- Vertical synchronization

This demonstrated that the internal FPGA video pipeline is independent of the final physical display interface.

The important architecture is:
```
`Video Source -> Streaming Pipeline -> Processing -> Video Sink`
```
The sink could be HDMI, VGA, memory, or another FPGA processing module.

**Recommended screenshot:** HDMI-input/VGA-output block design or physical VGA output demonstration.

---

## 5. AXI Interface Summary

The labs introduced three AXI communication styles that serve different purposes.

| Interface             | Purpose                         | Project Use              |
| --------------------- |-------------------------------- | ------------------------ |
| **AXI4-Lite**         | Low-bandwidth control registers | Configure custom FPGA IP |
| **AXI4-Stream**       | Continuous high-bandwidth data  | Transfer video pixels    |
| **AXI Memory-Mapped** | High-bandwidth memory access    | VDMA access to DDR       |

A typical system therefore contains two different communication paths.

### Control Path
```
`Cortex-A9 -> AXI4-Lite -> FPGA Registers`
```
Used for:

- Configuration
- Control commands
- Threshold values
- Start/stop operations
- Status registers

### Data Path
```
`Camera -> AXI4-Stream -> FPGA -> VDMA -> DDR`
```
Used for:

- Pixel data
- Frames
- High-bandwidth image information

Separating the control path from the data path is one of the key architectural concepts used throughout this project.

---

## 6. Moving Toward Hardware Image Acceleration

The previous stages establish the infrastructure required for the autonomous explorer project.

Currently, the video pipeline primarily transports image data:

`Camera -> Video Pipeline -> Display`

The next stage is to insert custom FPGA computation into that path:
```
`Camera`
`   ↓`
`Pixel Stream`
`   ↓`
`Custom FPGA Accelerator`
`   ├── Grayscale Conversion`
`   ├── Edge Detection`
`   └── Future Vision Processing`
`   ↓`
`Processed Results`
```
The Cortex-A9 will continue to configure and coordinate the accelerator through AXI4-Lite while the FPGA performs pixel operations in parallel.

This creates the hardware/software architecture:
```
`Cortex-A9`
`   |`
`AXI4-Lite`
`   ↓`
`FPGA Control Registers`
`   |`
`   ↓`
`Hardware Image Accelerator`
`   ↑`
`AXI4-Stream Video`
```
---

## 7. Connection to the Autonomous Explorer Project

The purpose of these FPGA exercises is not simply to display video.

Each stage develops a subsystem that will eventually contribute to the heterogeneous autonomous platform:
```
`Camera`
`   ↓`
`Zynq FPGA`
`   ├── Video Input`
`   ├── Grayscale`
`   ├── Edge Detection`
`   └── Hardware Acceleration`
`   ↓`
`ARM Cortex-A9`
`   ├── Accelerator Configuration`
`   ├── Performance Measurement`
`   └── System Coordination`
`   ↓`
`STM32`
`   ├── Ultrasonic / Sensor Input`
`   ├── PWM`
`   ├── Servo / Motor Control`
`   └── Real-Time Response`
```
The FPGA provides parallel computation for image-processing workloads, while the STM32 remains responsible for deterministic real-time sensing and actuator control.

Future experiments will measure the reaction time between obstacle detection and actuator response and compare different processing approaches, including:

- Pure C processing
- ARM NEON/SIMD acceleration
- Custom FPGA hardware acceleration
- STM32 real-time sensing and control

The goal is to evaluate where each processing architecture is most effective rather than forcing all tasks onto a single processor.
