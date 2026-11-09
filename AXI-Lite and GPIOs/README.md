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
