#pragma once
#include <stdint.h>
namespace XELP { namespace DSI {
struct SystemSleepImage {
// Explicit writable configuration owned by the parked DSI pipe. DDB and WM
// precede plane arming. Shared DBUF owns DDB/MBUS credits independently.
// No interrupt identity, status or frame counters.
static constexpr uint32_t config[] = {
    0x6001C,0x45270,
    0x70240,0x70244,0x70248,0x7024C,0x70250,0x70254,0x70258,0x7025C,0x70268,
    0x70140,0x70144,0x70148,0x7014C,0x70150,0x70154,0x70158,0x7015C,0x70168,
    0x70188,0x7018C,0x70190,0x701A4,0x701CC,0x70180,0x7019C,
    0xC8254,0xC8250
};
    // Linux intel_combo_phy.c: PHY-A voltage/process compensation, master
    // current reference and lane power-down enable. Retain only RW fields.
    struct Field {uint32_t reg, mask;};
    static constexpr Field fields[]={
        {0x162104,0x00FF00FF},{0x162124,UINT32_MAX},{0x162128,UINT32_MAX},
        {0x162120,1U<<24},{0x162100,1U<<31},{0x162014,1U<<4},
        {0x42084,1U<<30},{0xC7204,1U<<1},
        // Plane and cursor controls encode Display13 arbitration slots.
        // Select those slots on the pipe before rearming either consumer.
        {0x70028,1U<<13},
        // PHY A TX_DW8 stores the source/divider for duty-cycle correction.
        // Preserve each data lane's other trim fields.
        {0x1628A0,7U<<29},{0x1629A0,7U<<29},{0x162AA0,7U<<29},{0x162BA0,7U<<29}
    };
    static constexpr unsigned fieldCount=sizeof(fields)/sizeof(fields[0]);
    uint32_t savedFields[fieldCount] {};
    static constexpr unsigned count=sizeof(config)/sizeof(config[0]);
    uint32_t regs[count] {};
    bool captured {};
};
}}
