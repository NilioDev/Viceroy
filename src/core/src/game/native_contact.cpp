// SPDX-License-Identifier: GPL-3.0-or-later
//
// First contact with a tribe: when a unit comes to stand next to a people
// its nation has not met, the tribe welcomes it and offers peace. Read from
// the original with a debugger and checked against its answers; see
// docs/rules/natives.md.
#include "viceroy/game/game.hpp"
#include "viceroy/game/natives.hpp"

#include <algorithm>
#include <array>

namespace viceroy {

namespace {

// Relations: the nation's byte 52 + other, the tribe's byte 58 + nation.
constexpr std::size_t kNationRelations = 52;
constexpr std::size_t kTribeRelations = 58;
constexpr std::uint8_t kMet = 0x20;
constexpr std::uint8_t kPeace = 0x40;
// The tribe's 16-bit word at 46 + 2 × nation is 2 after the meeting,
// whatever the answer (its use is not read yet).
constexpr std::size_t kTribeMeeting = 46;
constexpr int kMeetingMark = 2;
constexpr int kMeetingTension = 20; // the tribe is at most this tense once met
constexpr int kRefusedTension = 100;
constexpr int kWelcomeTension =
    25; // INDIANCOME needs the tribe calmer than this
// The number of settlements of each tribe, in the block after the tribe
// records.
constexpr std::size_t kSettlementCounts =
    static_cast<std::size_t>(kTribes) * kTribeRecordSize + 581;

std::size_t tribeByte(int tribe, std::size_t at) {
  return static_cast<std::size_t>(tribe) * kTribeRecordSize + at;
}

// Sets bits in the relation on both sides.
void addRelation(SaveGame &save, int nation, int tribe, std::uint8_t bits) {
  auto &relation = save.nations[static_cast<std::size_t>(nation)]
                       .raw[kNationRelations +
                            static_cast<std::size_t>(kEuropeanNations + tribe)];
  relation = static_cast<std::uint8_t>(relation | bits);
  if (save.tribesAndMore.size() >= tribeByte(tribe, kTribeRecordSize)) {
    auto &back = save.tribesAndMore[tribeByte(
        tribe, kTribeRelations + static_cast<std::size_t>(nation))];
    back = static_cast<std::uint8_t>(back | bits);
  }
}

// The original looks at the eight neighbours in this order.
constexpr std::array<int, 8> kDx{0, 1, 1, 1, 0, -1, -1, -1};
constexpr std::array<int, 8> kDy{-1, -1, 0, 1, 1, 1, 0, -1};

} // namespace

void Game::meetTribe(int nation, int tribe) {
  if (nation < 0 || nation >= kEuropeanNations || tribe < 0 ||
      tribe >= kTribes) {
    return;
  }
  addRelation(save_, nation, tribe, kMet);
  auto &block = save_.tribesAndMore;
  if (block.size() >= tribeByte(tribe, kTribeRecordSize)) {
    const auto mark =
        tribeByte(tribe, kTribeMeeting + 2 * static_cast<std::size_t>(nation));
    block[mark] = kMeetingMark;
    block[mark + 1] = 0;
  }
  setTribeTension(
      save_, tribe, nation,
      std::min(tribeTension(save_, tribe, nation), kMeetingTension));
}

bool Game::meetNeighbours(int index) {
  const auto &u = unit(index);
  const auto &map = save_.map;
  if (!onMap(index) || !map.inside(u.x, u.y) || map.at(u.x, u.y).isWater()) {
    return false;
  }
  // Whose square is it: the nation of a unit there, or of the colony or
  // settlement.
  const auto owner = [&](int x, int y) {
    const auto here = unitsAt(x, y);
    if (!here.empty()) {
      return unit(here.front()).nation;
    }
    if (const auto c = colonyAt(x, y)) {
      return save_.colonies[static_cast<std::size_t>(*c)].nation;
    }
    if (const auto s = settlementAt(x, y)) {
      return save_.settlements[static_cast<std::size_t>(*s)].nation;
    }
    return -1;
  };
  bool asked = false;
  std::array<bool, kEuropeanNations + kTribes> seen{};
  for (std::size_t d = 0; d < kDx.size(); ++d) {
    const int x = u.x + kDx[d];
    const int y = u.y + kDy[d];
    if (!map.inside(x, y) || map.at(x, y).isWater()) {
      continue;
    }
    const int other = owner(x, y);
    if (other < 0 || other == u.nation || other >= kEuropeanNations + kTribes ||
        seen[static_cast<std::size_t>(other)]) {
      continue;
    }
    seen[static_cast<std::size_t>(other)] = true;
    // A European nation meeting a tribe (in either order); Europeans
    // meeting each other are not done yet.
    const bool europeanHere = u.nation < kEuropeanNations;
    const int nation = europeanHere ? u.nation : other;
    const int tribe = (europeanHere ? other : u.nation) - kEuropeanNations;
    if (nation >= kEuropeanNations || tribe < 0 || tribe >= kTribes) {
      continue;
    }
    const auto relation =
        save_.nations[static_cast<std::size_t>(nation)]
            .raw[kNationRelations +
                 static_cast<std::size_t>(kEuropeanNations + tribe)];
    if ((relation & kMet) != 0) {
      continue;
    }
    meetTribe(nation, tribe);
    if (nation != human()) {
      // A computer nation always takes the treaty.
      addRelation(save_, nation, tribe, kPeace);
      continue;
    }
    // "The Iroquois tribe welcomes you. We are a glorious nation of 11
    // Villages." The screen fills in the level's plural.
    const auto &tribes = rules().tribes;
    const auto t = static_cast<std::size_t>(tribe);
    const int settlements = save_.tribesAndMore.size() > kSettlementCounts + t
                                ? save_.tribesAndMore[kSettlementCounts + t]
                                : 0;
    GameNotice notice{"INDIANWELCOME",
                      {t < tribes.size() ? tribes[t].adjective : "", ""}};
    notice.value = tribe;
    notice.numbers = {settlements};
    notice.unit = index;
    notices_.push_back(std::move(notice));
    asked = true;
  }
  return asked;
}

void Game::answerWelcome(int tribe, bool accept) {
  const int nation = human();
  if (nation < 0 || nation >= kEuropeanNations || tribe < 0 ||
      tribe >= kTribes) {
    return;
  }
  const auto &tribes = rules().tribes;
  const auto t = static_cast<std::size_t>(tribe);
  const std::string name = t < tribes.size() ? tribes[t].name : "";
  if (accept) {
    addRelation(save_, nation, tribe, kPeace);
    // "The Iroquois welcome peace with our brothers the Dutch."
    GameNotice peace{"INDIANPEACE", {name, ""}};
    peace.value = tribe;
    peace.adjectives = {{1, nation}};
    notices_.push_back(std::move(peace));
    if (tribeTension(save_, tribe, nation) < kWelcomeTension) {
      GameNotice come{"INDIANCOME",
                      {t < tribes.size() ? tribes[t].adjective : ""}};
      come.value = tribe;
      notices_.push_back(std::move(come));
    }
    return;
  }
  // Measured: no treaty and the tribe at 100 tension (from 2).
  setTribeTension(save_, tribe, nation,
                  std::min(tribeTension(save_, tribe, nation) + kRefusedTension,
                           kRefusedTension));
  GameNotice shun{"INDIANSHUN", {name}};
  shun.value = tribe;
  notices_.push_back(std::move(shun));
}

} // namespace viceroy
