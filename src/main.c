#include <stdint.h>
#include "xparameters.h"

int main(void)
{
    volatile uint32_t value;

    while (1)
    {
        value = *(volatile uint32_t *)
                (XPAR_CUSTOMIP_0_BASEADDR + 0x00U);

        *(volatile uint32_t *)
        (XPAR_CUSTOMIP_0_BASEADDR + 0x04U) = value;
    }

    return 0;
}