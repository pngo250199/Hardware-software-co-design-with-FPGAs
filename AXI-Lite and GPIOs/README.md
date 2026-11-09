## AXI4-Lite Hardware/Software Co-Design

### What is AXI4-Lite?

AXI4-Lite is a lightweight memory-mapped communication interface used to connect processors with hardware peripherals. In the Zynq-7000, AXI allows software running on the ARM Cortex-A9 processing system to communicate directly with custom hardware implemented inside the FPGA programmable logic.

Unlike a normal software function call, AXI communication uses memory addresses. Each hardware register inside the FPGA is assigned an address, and the processor can read or write that register using standard C memory access.

The basic communication path in this project is:

`ARM Cortex-A9 → AXI4-Lite → Custom FPGA IP → GPIO`

This interface is important for hardware/software co-design because it allows software to configure FPGA hardware, provide input data, and retrieve results from custom hardware accelerators.

---

### Creating the Custom AXI4-Lite Peripheral

The first hardware/software co-design milestone was based on creating a custom AXI4-Lite peripheral in Vivado.

The peripheral uses a subordinate AXI interface connected to the Zynq Processing System through an AXI interconnect. The initial design uses 32-bit memory-mapped registers for communication between the Cortex-A9 and FPGA logic.

The custom IP contains two main Verilog files:

- A top-level module that exposes the external GPIO signals.
- An AXI slave module that implements the memory-mapped registers and AXI read/write logic.

The initial GPIO interface contains:

```verilog
input wire [3:0] gpio_in;
output wire [3:0] gpio_out;
```

These signals are connected to the physical switches and LEDs on the Zybo Z7 board.

---

### AXI Register Map

The first implementation uses the following register addresses:

| Address | Register | Purpose |
| --- | --- | --- |
| `0x43C00000` | Register 0 | Read switch/input state |
| `0x43C00004` | Register 1 | Control LED/output state |

This follows the same memory-mapped organization used in the original AXI GPIO lab, where the data register is located at `0x43C00000` and the second register at `0x43C00004`.

---

### Reading Physical Inputs

The AXI read logic was modified so that the lower four bits of Register 0 represent the physical switch inputs.

```verilog
{slv_reg0[C_S_AXI_DATA_WIDTH-1:4], gpio_in[3:0]}
```

This means that when the Cortex-A9 reads Register 0, bits `[3:0]` contain the current state of the four switches.

The original lab uses the same idea by replacing the lower four bits of the first AXI register with `sw[3:0]`.

For example:

```text
SW3 SW2 SW1 SW0
 0   1   0   1

Register value = 0x5
```

---

### Controlling the LEDs

Register 1 is connected directly to the four FPGA output signals:

```verilog
assign gpio_out = slv_reg1[3:0];
```

The lab performs the same function by connecting the LED output to `slv_reg1[3:0]`.

Therefore, when software writes a value into Register 1, the lower four bits immediately control the physical LEDs.

For example:

```text
Register 1 = 0xF
             ↓
         1111 binary
             ↓
LED3 LED2 LED1 LED0
 ON   ON   ON   ON
```

---

### Cortex-A9 Software Control

The Cortex-A9 accesses the FPGA registers using memory-mapped pointers in C.

```c
volatile uint32_t value;

value = *(volatile uint32_t *)
        (XPAR_CUSTOMIP_0_BASEADDR + 0x00U);

*(volatile uint32_t *)
        (XPAR_CUSTOMIP_0_BASEADDR + 0x04U) = value;
```

The software performs two operations:

```text
1. Read Register 0
      ↓
   Get switch state

2. Write value to Register 1
      ↓
   Update LEDs
```

The original lab follows the same sequence: read the switch value from `0x43C00000` and write that value to `0x43C00004`, allowing the LEDs to mirror the switch state.

---

### Complete Hardware/Software Data Path

The completed system can be represented as:

```text
Physical Switch
      │
      ▼
  gpio_in[3:0]
      │
      ▼
 AXI Register 0
      │
      │ AXI4-Lite Read
      ▼
 ARM Cortex-A9
      │
      │ AXI4-Lite Write
      ▼
 AXI Register 1
      │
      ▼
 gpio_out[3:0]
      │
      ▼
 Physical LEDs
```

The AXI interconnect connects the custom subordinate peripheral to the ARM processor's AXI manager interface.

---

### Hardware Validation

The first hardware test was intentionally simple: use the Cortex-A9 to control the physical LEDs through the custom FPGA peripheral.

The first successful test wrote:

```c
*(volatile uint32_t *)
    (XPAR_CUSTOMIP_0_BASEADDR + 0x04U) = 0xF;
```

The expected hardware result was:

```text
0xF
 ↓
1111
 ↓
All four LEDs ON
```

When all four LEDs turned on, the experiment verified the complete communication path:

`Cortex-A9 Software → AXI4-Lite → Custom FPGA IP → FPGA Register → GPIO → LED`

This was an important milestone because it confirmed that the ARM processing system could successfully control custom FPGA hardware through memory-mapped AXI communication.

---

### Why This Matters for the Car Project

The LED experiment is not the final purpose of the AXI interface. It is a simple hardware validation step used to verify that the processor and FPGA can communicate correctly.

The same architecture can later be reused for more useful hardware accelerators.

For example:

```text
Cortex-A9
   │
   ├── Set image threshold
   ├── Start accelerator
   └── Read accelerator status
            │
            ▼
        AXI4-Lite
            │
            ▼
      FPGA Accelerator
            │
            ├── Grayscale conversion
            ├── Edge detection
            └── Image processing
```

Instead of controlling LEDs, future AXI registers can be used for:

- Accelerator control
- Start/stop commands
- Image-processing parameters
- Threshold values
- Status flags
- Execution results
- Performance measurements

The LED test therefore establishes the communication foundation required for the FPGA-based image-processing and autonomous-system components that will be added later.
