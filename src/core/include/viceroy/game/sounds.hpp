// SPDX-License-Identifier: GPL-3.0-or-later
//
// The sound driver commands the game sends for events, as read from the
// original. When each plays is in docs/rules/sound.md.
#pragma once

namespace viceroy::sounds {

inline constexpr int kScuffle = 0x40;  // a fight where nobody has guns, or a beaten unit in the open
inline constexpr int kMuskets = 0x41;  // a fight with guns
inline constexpr int kCannon = 0x42;   // a ship or artillery fights
inline constexpr int kCannonHit = 0x43; // a ship or artillery wins
inline constexpr int kBravesWin = 0x45; // braves win at a colony
inline constexpr int kVillageHit = 0x48; // a village loses a person
inline constexpr int kAtHome = 0x49;   // a unit beaten in a colony or a village
inline constexpr int kBurned = 0x4A;   // a colony's or a village's last person falls
inline constexpr int kColonyTaken = 0x4B; // also a raid on a ship in the harbour
inline constexpr int kHorses = 0x4C;   // mounted men attack
inline constexpr int kShipBeaten = 0x4D;  // a ship loses to a ship, or to a raid
inline constexpr int kRaidGold = 0x4E;
inline constexpr int kRaidStores = 0x4F;
inline constexpr int kFire = 0x53;     // a raid burns a building, a colony burns, a mission burns
inline constexpr int kChiefKill = 0x55;
inline constexpr int kShipSunk = 0x57;
inline constexpr int kFortify = 0x58;
inline constexpr int kRaidNothing = 0x5B;
inline constexpr int kKing = 0x3E;   // the King changes the tax
inline constexpr int kParty = 0x56;  // a party throws goods into the sea
inline constexpr int kWarCry = 0x3B;   // plus the brave's unit type: braves fall on the player's men
inline constexpr int kChurch = 0x8024; // a mission, a missionary, a tribe's conversion

} // namespace viceroy::sounds
