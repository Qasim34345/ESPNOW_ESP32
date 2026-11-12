#pragma once
#include <stdint.h>
#include <stddef.h>

// This board's local MAC is discovered automatically; list only other nodes here.
// For broadcast, set ESPNOW_EXAMPLE_PEER_COUNT to 0.
// Defaults are for the second board as RX with the first board as its peer.
// On the first board, use TX_ONLY and replace this entry with the second board's MAC.
static constexpr uint8_t ESPNOW_EXAMPLE_PEERS[][6] = {
    // {0xDC,0xDA,0x0C,0x12,0x80,0xF8},
    {0xDC,0xDA,0x0C,0x18,0x0C,0x10},
};
static constexpr size_t ESPNOW_EXAMPLE_PEER_COUNT = sizeof(ESPNOW_EXAMPLE_PEERS) / sizeof(ESPNOW_EXAMPLE_PEERS[0]);
