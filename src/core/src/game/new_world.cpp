// SPDX-License-Identifier: GPL-3.0-or-later
//
// Start a Game in NEW WORLD: the map made up at random, step by step as the
// original does it. See docs/rules/new-world.md for the rules in words.
#include "viceroy/game/new_world.hpp"

#include "viceroy/game/resource.hpp"

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace viceroy {

namespace {

constexpr int kWidth = 58;
constexpr int kHeight = 72;

constexpr std::uint8_t kTundra = 0;
constexpr std::uint8_t kArctic = 0x18;
constexpr std::uint8_t kOcean = 0x19;
constexpr std::uint8_t kSeaLane = 0x1A;
constexpr std::uint8_t kHills = 0x20;
constexpr std::uint8_t kRiver = 0x40;
constexpr std::uint8_t kMountains = 0x80;
constexpr std::uint8_t kReliefBits = 0xE0;

// Directions clockwise from north; the ninth stands still.
constexpr std::array<int, 9> kDx{0, 1, 1, 1, 0, -1, -1, -1, 0};
constexpr std::array<int, 9> kDy{-1, -1, 0, 1, 1, 1, 0, -1, 0};
// The four straight neighbours.
constexpr std::array<int, 4> kDx4{0, 1, 0, -1};
constexpr std::array<int, 4> kDy4{-1, 0, 1, 0};
// The 5 × 5 square around a square without its middle and corners.
constexpr std::array<int, 20> kDx20{0, 1, 0, -1, -1, 1, 1, -1, 0, 2, 0, -2, -1, 1, -1, 1, -2, -2, 2, 2};
constexpr std::array<int, 20> kDy20{-1, 0, 1, 0, -1, -1, 1, 1, -2, 0, 2, 0, -2, -2, 2, 2, -1, 1, -1, 1};

enum Layer { kTerrain, kMask, kPath, kScratch };

// The original keeps its four map layers one after another in memory and
// reads them without a check of the edges: a square one past the end of a
// row is the first of the next row. Viceroy keeps them the same way.
class Layers {
public:
    Layers(int w, int h) : w_(w), h_(h), size_(w * h + 32) {
        mem_.assign(static_cast<std::size_t>(size_ * 6), kOcean);
        for (int l = 0; l < 4; ++l) {
            fill(static_cast<Layer>(l), 0);
        }
    }
    std::uint8_t get(Layer l, int x, int y) const {
        const long at = index(l, x, y);
        return at >= 0 && at < static_cast<long>(mem_.size()) ? mem_[static_cast<std::size_t>(at)] : kOcean;
    }
    void set(Layer l, int x, int y, int v) {
        const long at = index(l, x, y);
        if (at >= 0 && at < static_cast<long>(mem_.size())) {
            mem_[static_cast<std::size_t>(at)] = static_cast<std::uint8_t>(v);
        }
    }
    void fill(Layer l, int v) { std::fill_n(mem_.begin() + start(l), w_ * h_, static_cast<std::uint8_t>(v)); }
    void copy(Layer to, Layer from) { std::copy_n(mem_.begin() + start(from), w_ * h_, mem_.begin() + start(to)); }

private:
    long start(Layer l) const { return static_cast<long>(size_) * (1 + static_cast<int>(l)); }
    long index(Layer l, int x, int y) const { return start(l) + static_cast<long>(y) * w_ + x; }

    int w_;
    int h_;
    int size_;
    std::vector<std::uint8_t> mem_;
};

class Generator {
public:
    Generator(const NewWorldSettings& s, OriginalRandom& random)
        : p_(s.value), random_(random), layers_(kWidth, kHeight) {}

    NewWorld run(int humanNation);

private:
    int R(int lo, int hi) { return random_.between(lo, hi); }
    std::uint8_t A(int x, int y) const { return layers_.get(kTerrain, x, y); }
    void setA(int x, int y, int v) { layers_.set(kTerrain, x, y, v); }
    std::uint8_t B(int x, int y) const { return layers_.get(kMask, x, y); }
    void setB(int x, int y, int v) { layers_.set(kMask, x, y, v); }
    std::uint8_t C(int x, int y) const { return layers_.get(kScratch, x, y); }
    void setC(int x, int y, int v) { layers_.set(kScratch, x, y, v); }

    bool inside(int x, int y) const { return x >= 1 && y >= 1 && x < w_ - 1 && y < h_ - 1; }
    bool isWater(int x, int y) const {
        const int t = A(x, y) & 0x1F;
        return t == kOcean || t == kSeaLane;
    }
    // The terrain as the game's tables number it: 0x19 off the inner map,
    // 0x1B mountains, 0x1C hills.
    int terrainType(int x, int y) const {
        if (!inside(x, y)) {
            return kOcean;
        }
        const int v = A(x, y);
        if ((v & kHills) != 0) {
            return (v & kMountains) != 0 ? 0x1B : 0x1C;
        }
        return v & 0x1F;
    }
    bool coastal(int x, int y) const {
        for (int d = 0; d < 8; ++d) {
            const int nx = x + kDx[static_cast<std::size_t>(d)];
            const int ny = y + kDy[static_cast<std::size_t>(d)];
            if (inside(nx, ny) && isWater(nx, ny)) {
                return true;
            }
        }
        return false;
    }
    bool walkInside(int x, int y) const { return x0_ < x && x < x1_ && y0_ < y && y < y1_; }

    void plot(int x, int y);
    void walkDiagonal(int x, int y);
    void walkWide(int x, int y);
    void walkStraight(int x, int y);
    void addLand(bool island);
    int labelRegions();
    void joinDiagonals();
    void latitudes();
    void moisture();
    void relief();
    void forests();
    void rivers();
    bool flattenMountain(int x, int y);
    void finish(int humanNation, NewWorld& out);

    std::array<int, 5> p_;
    OriginalRandom& random_;
    Layers layers_;
    int w_ = kWidth;
    int h_ = kHeight;
    int land_ = 0;
    int x0_ = 0;
    int x1_ = 0;
    int y0_ = 0;
    int y1_ = 0;
    int seed_ = 0;
    std::array<int, 16> regionSizes_{};
    // Kept from square to square by the hill pass, as in the original.
    int hillOdds_ = 0;
    int mountainOdds_ = 0;
};

void Generator::plot(int x, int y) {
    if (x == 0 || y == 0 || x >= w_ || y >= h_) {
        return;
    }
    setC(x, y, 1);
    if (x < w_ - 1) {
        setC(x + 1, y, 1);
    }
    if (y < h_ - 1) {
        setC(x, y + 1, 1);
    }
}

void Generator::walkDiagonal(int x, int y) {
    for (int steps = R(1, 64) + 2; steps-- != 0;) {
        if (!walkInside(x, y)) {
            break;
        }
        plot(x, y);
        const auto d = static_cast<std::size_t>(2 * R(1, 4) - 1);
        x += kDx[d];
        y += kDy[d];
    }
}

void Generator::walkWide(int x, int y) {
    for (int steps = R(1, 48) + 2; steps-- != 0;) {
        if (!walkInside(x, y)) {
            break;
        }
        plot(x, y);
        if (R(1, 4) == 1) {
            plot(x + 1, y + 1);
        }
        if (R(1, 4) == 1) {
            plot(x - 1, y + 1);
        }
        if (R(1, 4) == 1) {
            plot(x + 1, y - 1);
        }
        if (R(1, 4) == 1) {
            plot(x - 1, y - 1);
        }
        const auto d = static_cast<std::size_t>(2 * R(1, 4) - 1);
        x += kDx[d];
        y += kDy[d];
    }
}

void Generator::walkStraight(int x, int y) {
    for (int steps = R(1, 16) + 2; steps-- != 0;) {
        if (!walkInside(x, y)) {
            break;
        }
        setC(x, y, 1);
        const auto d = static_cast<std::size_t>(2 * (R(1, 4) - 1));
        x += kDx[d];
        y += kDy[d];
    }
}

void Generator::addLand(bool island) {
    layers_.fill(kScratch, 0);
    int x = 0;
    int y = 0;
    do {
        x = R(1, w_ - 16) + 7;
        y = R(1, h_ - 8) + 3;
    } while (island && B(x, y) != 0);
    if (island) {
        const int n = R(1, 10);
        walkStraight(x, y);
        if (n >= 7) {
            walkStraight(x, y);
        }
        if (n >= 8) {
            walkStraight(x, y);
        }
    } else if (p_[1] >= 2) {
        walkWide(x, y);
    } else {
        walkDiagonal(x, y);
    }
    for (int yy = 0; yy < h_; ++yy) {
        for (int xx = 0; xx < w_; ++xx) {
            if (C(xx, yy) != 0) {
                setB(xx, yy, B(xx, yy) + 1);
                ++land_;
            }
        }
    }
}

// Numbers the bodies of water, then of land, 1 to 15 into the path layer
// (8 squares around count as joined). Returns the sizes of the land's.
int Generator::labelRegions() {
    int overflow = 0;
    const int n = w_ * h_;
    std::vector<int> label(static_cast<std::size_t>(n + w_ + 1));
    std::vector<int> count(0x4000 + 2);
    layers_.fill(kPath, 0);
    for (int water = 1; water >= 0; --water) {
        std::fill(count.begin(), count.end(), 0);
        std::fill(label.begin(), label.end(), 0);
        const auto at = [&](int x, int y) -> int& { return label[static_cast<std::size_t>(y * w_ + x)]; };
        // The run of squares goes on from the end of one row to the start
        // of the next, as in the original.
        int cur = 0;
        for (int y = 1; y < h_ - 1; ++y) {
            for (int x = w_ - 2; x >= 1; --x) {
                if (!inside(x, y) || static_cast<int>(isWater(x, y)) != water) {
                    cur = 0;
                    continue;
                }
                for (int dx = -1; dx <= 1; ++dx) {
                    const int lab = at(x + dx, y - 1);
                    if (lab == 0 || cur == 0 || lab == cur) {
                        if (lab != 0) {
                            cur = lab;
                        }
                        continue;
                    }
                    const int hi = std::max(lab, cur);
                    const int lo = std::min(lab, cur);
                    count[static_cast<std::size_t>(lo)] += count[static_cast<std::size_t>(hi)];
                    count[static_cast<std::size_t>(hi)] = 0;
                    for (int i = w_ + 1; i <= (y + 1) * w_; ++i) {
                        if (label[static_cast<std::size_t>(i)] == hi) {
                            label[static_cast<std::size_t>(i)] = lo;
                        }
                    }
                    cur = lo;
                }
                if (cur == 0) {
                    // Land along the second or the last but one row is
                    // numbered after the rest.
                    int next = (y == 1 || h_ - y == 2) && water == 0 ? 0x10 : 0;
                    do {
                        ++next;
                    } while (count[static_cast<std::size_t>(next)] != 0 && next < 0x4002);
                    cur = std::min(next, 0x3FFF);
                }
                at(x, y) = cur;
                ++count[static_cast<std::size_t>(cur)];
            }
        }
        for (int i = 0; i < n; ++i) {
            int& v = label[static_cast<std::size_t>(i)];
            if (v == 0) {
                continue;
            }
            if (v > 0xF) {
                int& c = count[static_cast<std::size_t>(v)];
                if (c > 0) {
                    int s = 1;
                    while (count[static_cast<std::size_t>(s)] != 0) {
                        ++s;
                    }
                    if (s <= 0xF) {
                        count[static_cast<std::size_t>(s)] = c;
                        c = -s;
                        v = s;
                    } else {
                        overflow |= 1 << water;
                        v = 0xF;
                    }
                } else {
                    v = -c;
                }
            }
            layers_.set(kPath, i % w_, i / w_, v);
        }
    }
    for (std::size_t i = 0; i < regionSizes_.size(); ++i) {
        regionSizes_[i] = count[i];
    }
    return overflow;
}

void Generator::joinDiagonals() {
    // Land touching only at a corner is joined into a block of four.
    for (int y = 1; y < h_ - 1; ++y) {
        for (int x = 1; x < w_ - 1; ++x) {
            const int m = (B(x, y) != 0 ? 1 : 0) | (B(x + 1, y) != 0 ? 2 : 0) | (B(x, y + 1) != 0 ? 4 : 0) |
                          (B(x + 1, y + 1) != 0 ? 8 : 0);
            if (m != 6 && m != 9) {
                continue;
            }
            setB(x + 1, y, 1);
            setB(x, y + 1, 1);
            setB(x + 1, y + 1, 1);
            if (x != 0) {
                --x;
            }
            if (y != 0) {
                --y;
            }
        }
    }
}

void Generator::latitudes() {
    constexpr std::array<int, 6> kBand{5, 4, 1, 3, 2, 2};
    for (int y = 0; y < h_; ++y) {
        for (int x = 0; x < w_; ++x) {
            const int layers = B(x, y);
            const int side = h_ / 2 - R(1, 16) - y + 8;
            int a = h_ / 2 - R(1, 16) - y + 8;
            if (side <= 0) {
                a = -a;
            }
            a += (1 - p_[2]) * 2;
            a = std::max(a, 0) >> 2;
            int t = a <= 5 ? kBand[static_cast<std::size_t>(a)] : kTundra;
            if (layers == 0) {
                t = kOcean;
            }
            if (layers >= 2) {
                t |= kHills;
            }
            if (layers >= 3) {
                t |= kMountains;
            }
            setA(x, y, t);
        }
    }
}

void Generator::moisture() {
    for (int y = 0; y < h_; ++y) {
        const int d = std::abs(h_ / 2 - y);
        const int reach = std::abs(h_ / 4 - d) + 4 * p_[3];
        int c = R(0, reach);
        for (int x = 0; x < w_; ++x) {
            int v = A(x, y);
            int k = v & 0x1F;
            if (v == kOcean) {
                if (reach > c) {
                    ++c;
                }
            } else {
                if ((v & kMountains) != 0) {
                    c -= 3;
                } else if ((v & kHills) != 0) {
                    v &= 0x5F;
                } else if (c < 0) {
                    switch (k) {
                    case 0:
                        setB(x, y, 2);
                        break;
                    case 2:
                        k = 0;
                        break;
                    case 3:
                        if (R(0, std::abs(c)) != 0) {
                            k = 1;
                        } else {
                            k = 2;
                            --c;
                        }
                        break;
                    case 4:
                        k = 3;
                        break;
                    default:
                        break;
                    }
                } else if (c > 0) {
                    switch (k) {
                    case 0:
                        k = 2;
                        break;
                    case 2:
                        k = 3;
                        break;
                    case 3:
                        k = 4;
                        break;
                    case 4:
                        c -= 2;
                        if (R(0, 3) == 0) {
                            k = 6;
                        }
                        break;
                    case 5:
                        c -= 2;
                        if (R(0, 3) == 0) {
                            k = 7;
                        }
                        break;
                    default:
                        break;
                    }
                }
                if (c > 0) {
                    c -= R(1, 7 - 2 * p_[3]);
                } else if (c < 0) {
                    ++c;
                }
            }
            setA(x, y, (v & kReliefBits) | k);
        }
        c = 0;
        for (int x = w_ - 1; x >= 0; --x) {
            int v = A(x, y);
            int k = v & 0x1F;
            if (k == kOcean) {
                if ((d >> 1) + p_[3] > c) {
                    ++c;
                }
            } else {
                if ((v & kMountains) != 0) {
                    c -= 3;
                } else if ((v & kHills) != 0) {
                    v &= 0x5F;
                } else if (c > 0) {
                    switch (k) {
                    case 0:
                        k = 2;
                        break;
                    case 1:
                    case 2:
                        k = 3;
                        break;
                    case 3:
                        k = 4;
                        break;
                    case 4:
                        c -= 2;
                        if (R(0, 1) == 0) {
                            k = 6;
                        }
                        break;
                    case 5:
                        c -= 2;
                        k = 7;
                        break;
                    default:
                        break;
                    }
                }
                if (c > 0) {
                    c -= R(1, 7 - 2 * p_[3]);
                } else if (c < 0) {
                    ++c;
                }
            }
            setA(x, y, (v & kReliefBits) | k);
        }
    }
}

// A mountain with no ocean on its four corners is worn down.
bool Generator::flattenMountain(int x, int y) {
    if (!inside(x, y)) {
        return false;
    }
    if (A(x - 1, y - 1) == kOcean || A(x - 1, y + 1) == kOcean || A(x + 1, y - 1) == kOcean ||
        A(x + 1, y + 1) == kOcean) {
        return false;
    }
    setA(x, y, kOcean);
    return true;
}

void Generator::relief() {
    int x = 0;
    int y = 0;
    const int rounds = (p_[4] + 1) * 800;
    for (int i = 0; i < rounds; ++i) {
        if ((i & 1) != 0) {
            const auto d = static_cast<std::size_t>(R(0, 8));
            x += kDx[d];
            y += kDy[d];
        } else {
            x = R(1, w_ - 2);
            y = R(1, h_ - 2);
        }
        int v = A(x, y);
        int k = v & 0x1F;
        if ((v & kMountains) != 0) {
            if (flattenMountain(x, y)) {
                v &= 0x5F;
            }
        } else if ((v & kHills) != 0) {
            v |= kMountains;
            setB(x, y, 1);
        } else {
            hillOdds_ = 0;
            mountainOdds_ = 0;
            const auto mark = [&](int value) { setB(x, y, value); };
            switch (k) {
            case 0:
                hillOdds_ = 1;
                if (R(0, 1) == 0) {
                    k = 2;
                }
                break;
            case 1:
                hillOdds_ = mountainOdds_ = 1;
                if (R(0, 1) == 0) {
                    k = 3;
                }
                break;
            case 2:
            case 3:
                if (k == 3 && R(0, 2) == 0) {
                    k = 2;
                }
                hillOdds_ = mountainOdds_ = 2;
                if (R(0, 1) == 0) {
                    mark(2);
                }
                break;
            case 4:
            case 5:
                hillOdds_ = 3;
                mountainOdds_ = k == 4 ? 1 : 2;
                if (R(0, 1) == 0) {
                    k = k == 4 ? 6 : 7;
                }
                if (R(0, 1) == 0) {
                    mark(1);
                }
                break;
            case 6:
                hillOdds_ = 5;
                mountainOdds_ = 3;
                if (R(0, 1) == 0) {
                    k = 4;
                }
                if (R(0, 1) == 0) {
                    mark(2);
                }
                break;
            case 7:
                hillOdds_ = 5;
                mountainOdds_ = 3;
                if (R(0, 1) != 0) {
                    k = 5;
                }
                if (R(0, 1) != 0) {
                    mark(2);
                }
                break;
            default:
                break;
            }
        }
        if (hillOdds_ != 0 && R(0, hillOdds_) == 0) {
            v |= kHills;
            if (mountainOdds_ != 0 && R(0, mountainOdds_) == 0) {
                v |= kMountains;
            }
        }
        setA(x, y, (v & kReliefBits) | k);
    }
}

void Generator::forests() {
    for (int y = 0; y < h_; ++y) {
        for (int x = 0; x < w_; ++x) {
            int v = A(x, y);
            if (v == kOcean) {
                continue;
            }
            if (B(x, y) == 1) {
                v += R(0, 8) != 0 ? 0x10 : 8;
            } else if (coastal(x, y)) {
                if (R(0, 1) == 0) {
                    v += 8;
                } else if (R(0, 4) != 0) {
                    v += 0x10;
                }
            }
            setA(x, y, v);
        }
    }
}

void Generator::rivers() {
    int count = 0;
    int tries = 0;
    int dir = 0;
    int turn = 0;
    int endX = 0;
    int endY = 0;
    do {
        layers_.copy(kScratch, kTerrain);
        ++tries;
        int length = 0;
        int x = 0;
        int y = 0;
        int v = 0;
        do {
            x = R(1, w_ - 2);
            y = R(1, h_ - 2);
            v = C(x, y);
        } while ((v & kHills) != 0 || isWater(x, y));
        const int sx = x;
        const int sy = y;
        dir = R(0, 3) * 2;
        turn = R(0, 1);
        bool reached = false;
        for (;;) {
            setA(x, y, v | kRiver);
            ++length;
            for (int j = 0; j < 4 && !reached; ++j) {
                const int nx = x + kDx4[static_cast<std::size_t>(j)];
                const int ny = y + kDy4[static_cast<std::size_t>(j)];
                if (isWater(nx, ny) || (C(nx, ny) & kRiver) != 0) {
                    reached = true;
                    setA(nx, ny, A(nx, ny) | kRiver);
                    endX = x;
                    endY = y;
                }
            }
            const int r = R(0, 99);
            int nd = dir;
            if (r >= 60) {
                if (r > 95) {
                    turn = turn == 0 ? 1 : 0;
                }
                nd = (dir + (turn != 0 ? 2 : 6)) % 8;
                turn = turn == 0 ? 1 : 0;
            }
            dir = nd;
            y += kDy[static_cast<std::size_t>(dir)];
            x += kDx[static_cast<std::size_t>(dir)];
            v = A(x, y);
            if (!reached && inside(x, y) && (v & kRiver) == 0 && (v & kHills) == 0) {
                continue;
            }
            break;
        }
        if ((reached || (v & kRiver) != 0) && length >= 3) {
            ++count;
            if (reached && R(1, (p_[3] + 6) * 2) > 6) {
                // A major river from the mouth up.
                for (int k = R(1, p_[3] * 2 + 3);;) {
                    setA(endX, endY, A(endX, endY) | kMountains);
                    int found = -1;
                    for (int j = 0; j < 4 && found < 0; ++j) {
                        const int nx = endX + kDx4[static_cast<std::size_t>(j)];
                        const int ny = endY + kDy4[static_cast<std::size_t>(j)];
                        const int n = A(nx, ny);
                        if ((n & kRiver) != 0 && (n & kMountains) == 0) {
                            found = j;
                            endX = nx;
                            endY = ny;
                        }
                    }
                    if (--k <= 0 || found < 0) {
                        break;
                    }
                }
            }
            // Woods around the spring.
            for (int j = 0; j < 20; ++j) {
                const int nx = sx + kDx20[static_cast<std::size_t>(j)];
                const int ny = sy + kDy20[static_cast<std::size_t>(j)];
                const int n = A(nx, ny);
                if ((n & 0x1F) < 0x10 && R(0, 1) != 0) {
                    setA(nx, ny, n + 8);
                }
            }
        } else {
            layers_.copy(kTerrain, kScratch);
        }
    } while (tries < 0x200 && (p_[3] + p_[0] + 2) * 8 > count);
}

NewWorld Generator::run(int humanNation) {
    seed_ = R(1, 0x7FFF);
    land_ = 0;
    layers_.fill(kTerrain, kOcean);
    layers_.fill(kMask, 0);
    x0_ = 3;
    x1_ = w_ - 6;
    y0_ = 0;
    y1_ = h_;
    if (R(0, 1) != 0) {
        y0_ = 5;
    } else {
        y1_ = h_ - 6;
    }
    const bool openNorth = y0_ != 0;
    do {
        addLand(false);
    } while ((p_[1] + p_[0] + 1) * 320 > land_);
    labelRegions();
    int bodies = 0;
    for (int s : regionSizes_) {
        bodies += s != 0 ? 1 : 0;
    }
    int islands = 15 - bodies;
    if (islands > 0) {
        if (p_[1] > 0) {
            islands -= R(0, islands);
        }
        for (int i = 0; i < islands; ++i) {
            addLand(true);
        }
    }
    joinDiagonals();
    latitudes();
    moisture();
    relief();
    forests();
    rivers();

    // The open sea along the side that was kept free of land.
    const int strip = openNorth ? 0 : h_ - 4;
    for (int y = strip; y < strip + 4; ++y) {
        for (int x = 0; x < w_; ++x) {
            setA(x, y, kOcean);
        }
    }
    // The rest of the edge is ocean, two squares in from the sides.
    for (int x = 2; x <= w_ - 3; ++x) {
        setA(x, 0, kOcean);
        setA(x, h_ - 1, kOcean);
    }
    for (int y = 0; y < h_; ++y) {
        setA(2, y, kOcean);
        setA(w_ - 3, y, kOcean);
    }
    for (int i = 0; i < 40; ++i) {
        setA(R(1, w_) - 1, 1, kArctic);
        setA(R(1, w_) - 1, h_ - 2, kArctic);
    }
    // Sea lanes from the east edge to the first land, as far as the middle.
    for (int y = 1; y < h_ - 1; ++y) {
        for (int x = w_ - 1; w_ / 2 <= x; --x) {
            if (!isWater(x, y)) {
                break;
            }
            setA(x, y, (A(x, y) & kReliefBits) + kSeaLane);
        }
    }
    // Ocean again within three squares of the easternmost land.
    for (int y = 1; y < h_ - 1; ++y) {
        int east = -1;
        for (int x = w_ - 1; x >= 1 && east < 0; --x) {
            if (!isWater(x, y)) {
                east = x;
            }
        }
        if (east < 0) {
            continue;
        }
        int x = std::min(east + 3, w_ - 2);
        for (int yy = y - 3; yy <= y + 3; ++yy) {
            if (!inside(x, yy)) {
                continue;
            }
            while (!isWater(x, yy) && w_ - 2 > x) {
                ++x;
            }
            if (isWater(x, yy)) {
                setA(x, yy, (A(x, yy) & kReliefBits) | kOcean);
            }
        }
    }
    // No sea lane west of the first stretch of it.
    for (int y = 1; y < h_ - 1; ++y) {
        bool lane = true;
        for (int x = w_ - 1; x >= 1; --x) {
            if (!lane) {
                if (isWater(x, y)) {
                    setA(x, y, (A(x, y) & kReliefBits) | kOcean);
                }
            } else if ((A(x, y) & 0x1F) != kSeaLane) {
                lane = false;
            }
        }
    }
    // Ice and tundra toward the poles.
    for (int pole = 0; pole < 2; ++pole) {
        for (int x = 0; x < w_; ++x) {
            int y = pole != 0 ? 1 : h_ - 2;
            if (!isWater(x, y)) {
                setA(x, y, kArctic);
            }
            y = pole != 0 ? 2 : h_ - 3;
            if (!isWater(x, y)) {
                const int t = terrainType(x, y);
                setA(x, y, (R(0, 1) >= 1 ? kArctic : kTundra) | (t & kReliefBits));
            }
            y = pole != 0 ? 3 : h_ - 4;
            if (R(0, 1) != 0 && !isWater(x, y)) {
                setA(x, y, terrainType(x, y) & kReliefBits);
            }
        }
    }
    NewWorld out;
    finish(humanNation, out);
    return out;
}

void Generator::finish(int humanNation, NewWorld& out) {
    // The edges: sea lane down the outer two columns each side, arctic
    // along the top and bottom.
    for (int x = 0; x < w_; ++x) {
        for (int y : {0, h_ - 1}) {
            setA(x, y, kSeaLane);
        }
    }
    for (int y = 0; y < h_; ++y) {
        for (int x : {0, 1, w_ - 2, w_ - 1}) {
            setA(x, y, kSeaLane);
        }
    }
    for (int x = 0; x < w_; ++x) {
        setA(x, 0, kArctic);
        setA(x, h_ - 1, kArctic);
    }
    // Hills and mountains carry no forest; the second numbering of
    // forest becomes the first.
    for (int x = 0; x < w_; ++x) {
        for (int y = 0; y < h_; ++y) {
            const int v = A(x, y);
            const int k = v & 0x1F;
            if (k >= kArctic) {
                continue;
            }
            if ((v & kHills) != 0) {
                setA(x, y, (v & kReliefBits) | (k & 7));
            } else if (k >= 0x10) {
                setA(x, y, v - 8);
            }
        }
    }
    labelRegions();
    layers_.fill(kMask, 0);
    layers_.fill(kScratch, 0);
    // The Pacific: the water of each row from the west up to the first
    // land, short of the middle.
    for (int y = 1; y < h_ - 1; ++y) {
        for (int x = 1; x < w_ / 2 && isWater(x, y); ++x) {
            setB(x, y, B(x, y) | MapSquare::kPacific);
        }
    }
    // Sea on the pattern of resources with no land near: no fish.
    for (int y = 0; y < h_; ++y) {
        for (int x = 0; x < w_; ++x) {
            // Sea lane has no resource (but the edge squares count as
            // ocean).
            if (!isWater(x, y) || terrainType(x, y) == kSeaLane) {
                continue;
            }
            const int cell = (x & 3) * 4 + (y & 3);
            const int shift = ((y >> 2) * 3 + (x >> 2) + (seed_ & 0xFF)) & 0xF;
            if (shift != cell && (shift ^ 0xA) != cell) {
                continue;
            }
            bool land = false;
            for (int j = 0; j < 20 && !land; ++j) {
                const int nx = x + kDx20[static_cast<std::size_t>(j)];
                const int ny = y + kDy20[static_cast<std::size_t>(j)];
                land = inside(nx, ny) && !isWater(nx, ny);
            }
            if (!land) {
                setB(x, y, B(x, y) | 0x04);
            }
        }
    }
    // The starting squares: the four rows a fifth of the map apart, at
    // the western end of the sea lane; the human player in one of the
    // middle two.
    std::array<int, 4> nationAt{-1, -1, -1, -1};
    for (int i = 0; i < 4; ++i) {
        const int nation = (i + humanNation) % 4;
        for (;;) {
            const int s = i == 0 ? R(1, 2) : R(0, 3);
            if (nationAt[static_cast<std::size_t>(s)] < 0) {
                nationAt[static_cast<std::size_t>(s)] = nation;
                break;
            }
        }
    }
    for (int i = 0; i < 4; ++i) {
        const int y = h_ / 5 * (i + 1);
        int x = w_ - 2;
        while (x > 2 && terrainType(x, y) == kSeaLane) {
            --x;
        }
        ++x;
        out.start[static_cast<std::size_t>(nationAt[static_cast<std::size_t>(i)])] = {x, y};
    }

    auto& map = out.map;
    map.width = w_;
    map.height = h_;
    map.resourceSeed = static_cast<std::uint8_t>(seed_);
    map.squares.resize(static_cast<std::size_t>(w_ * h_));
    for (int y = 0; y < h_; ++y) {
        for (int x = 0; x < w_; ++x) {
            auto& sq = map.at(x, y);
            const int v = A(x, y);
            sq.terrain = static_cast<std::uint8_t>(v & 0x1F);
            sq.relief = static_cast<std::uint8_t>(v >> 5);
            sq.flags = B(x, y);
            sq.region = layers_.get(kPath, x, y);
            sq.seen = 0;
        }
    }
    out.landRegionSizes = regionSizes_;
}

// The terrain of a square as the game's tables number it: 0x19 off the
// inner map, 0x1B mountains, 0x1C hills.
int squareType(const SaveMap& map, int x, int y) {
    if (x < 1 || y < 1 || x >= map.width - 1 || y >= map.height - 1) {
        return kOcean;
    }
    const auto& sq = map.at(x, y);
    if ((sq.relief & 1) != 0) {
        return (sq.relief & 4) != 0 ? 0x1B : 0x1C;
    }
    return sq.terrain & 0x1F;
}

bool innerSquare(const SaveMap& map, int x, int y) {
    return x >= 1 && y >= 1 && x < map.width - 1 && y < map.height - 1;
}

// The original reads the layers without checking the edges; a square past
// the end of a row is the first of the next one.
const MapSquare* flat(const SaveMap& map, int x, int y) {
    const long at = static_cast<long>(y) * map.width + x;
    return at >= 0 && at < static_cast<long>(map.squares.size()) ? &map.squares[static_cast<std::size_t>(at)] : nullptr;
}

bool flatWater(const SaveMap& map, int x, int y) {
    const auto* sq = flat(map, x, y);
    return sq == nullptr || sq->isWater();
}

int flatMask(const SaveMap& map, int x, int y) {
    const auto* sq = flat(map, x, y);
    return sq != nullptr ? sq->flags : 0;
}

int distance(int dx, int dy) {
    dx = std::abs(dx);
    dy = std::abs(dy);
    return std::max(dx, dy) + std::min(dx, dy) / 2;
}

// The settlement nearest a square (the later of equals), and how far.
int nearestSettlement(const SaveGame& save, int x, int y, int& howFar) {
    int best = -1;
    howFar = 9999;
    for (std::size_t i = 0; i < save.settlements.size(); ++i) {
        const auto& s = save.settlements[i];
        const int d = distance(s.x - x, s.y - y);
        if (d <= howFar) {
            howFar = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

constexpr std::size_t kTribeRecord = 78;
constexpr int kMaxSettlements = 84;
constexpr int kBrave = 19;
constexpr std::uint8_t kHasUnit = MapSquare::kHasUnit;
constexpr std::uint8_t kHasTown = MapSquare::kHasColony;

std::uint8_t* tribeAt(SaveGame& save, int tribe) {
    const auto at = kTribeRecord * static_cast<std::size_t>(tribe);
    return save.tribesAndMore.size() >= at + kTribeRecord ? save.tribesAndMore.data() + at : nullptr;
}

int tribeLevel(const Ruleset& rules, int tribe) {
    const auto t = static_cast<std::size_t>(tribe);
    return t < rules.tribes.size() ? rules.tribes[t].level : 0;
}

// A settlement as the original makes one, marked on the map.
int foundSettlement(SaveGame& save, const Ruleset& rules, int nation, int x, int y) {
    if (static_cast<int>(save.settlements.size()) >= kMaxSettlements) {
        return -1;
    }
    SaveSettlement s;
    s.x = x;
    s.y = y;
    s.nation = nation;
    s.population = 2 * tribeLevel(rules, nation - kEuropeanNations) + 3;
    s.raw.fill(0);
    s.raw[0] = static_cast<std::uint8_t>(x);
    s.raw[1] = static_cast<std::uint8_t>(y);
    s.raw[2] = static_cast<std::uint8_t>(nation);
    s.raw[4] = static_cast<std::uint8_t>(s.population);
    s.raw[5] = 0xFF;
    s.raw[7] = s.raw[8] = s.raw[9] = 0xFF;
    save.settlements.push_back(s);
    auto& sq = save.map.at(x, y);
    sq.flags |= kHasTown;
    sq.region = static_cast<std::uint8_t>((sq.region & 0x0F) | (nation << 4));
    return static_cast<int>(save.settlements.size()) - 1;
}

void makeCapital(SaveGame& save, int s) {
    auto& t = save.settlements[static_cast<std::size_t>(s)];
    t.raw[3] |= 4;
    t.raw[6] = static_cast<std::uint8_t>(t.population);
}

SaveUnit brave(int x, int y, int nation, int home) {
    SaveUnit u;
    u.x = x;
    u.y = y;
    u.type = kBrave;
    u.nation = nation;
    u.profession = 0x1C;
    u.raw.fill(0);
    u.raw[0] = static_cast<std::uint8_t>(x);
    u.raw[1] = static_cast<std::uint8_t>(y);
    u.raw[2] = kBrave;
    u.raw[3] = static_cast<std::uint8_t>(nation);
    u.raw[6] = static_cast<std::uint8_t>(home);
    u.raw[7] = 0x58;
    u.raw[22] = 1;
    u.workTurns = 1;
    u.raw[23] = 0x1C;
    u.raw[24] = u.raw[25] = u.raw[26] = u.raw[27] = 0xFF;
    return u;
}

// Fit for a settlement: open land or forest, not desert or swamp, nothing
// standing there.
bool settlementGround(const SaveMap& map, int x, int y) {
    if (!innerSquare(map, x, y) || (flatMask(map, x, y) & (kHasUnit | kHasTown)) != 0) {
        return false;
    }
    const int t = squareType(map, x, y);
    if (t >= kArctic) {
        return false;
    }
    const int ground = t & 7;
    return (ground >= 2 && ground <= 6) || ground == 0;
}

} // namespace

void initTribes(SaveGame& save, const Ruleset& rules, OriginalRandom& random) {
    for (int t = 0; t < kTribes; ++t) {
        auto* r = tribeAt(save, t);
        if (r == nullptr) {
            return;
        }
        for (int n = 0; n < kEuropeanNations; ++n) {
            const bool human = save.players[static_cast<std::size_t>(n)].control == 0;
            const int tension = random.between(0, 14) + (human ? 2 * save.difficulty : 0);
            r[70 + 2 * n] = static_cast<std::uint8_t>(tension);
            r[71 + 2 * n] = static_cast<std::uint8_t>(tension >> 8);
            r[54 + n] = 0;
        }
        std::fill(r + 58, r + 70, std::uint8_t{0});
        std::fill(r + 14, r + 46, std::uint8_t{0});
        r[0] = 1;
        r[1] = 1;
        r[2] = static_cast<std::uint8_t>(tribeLevel(rules, t));
        std::fill(r + 4, r + 14, std::uint8_t{0});
    }
}

void placeNewWorldNatives(SaveGame& save, const Ruleset& rules, OriginalRandom& random) {
    auto& map = save.map;
    const int w = map.width;
    const int h = map.height;
    constexpr int kCellsAcross = 15;
    constexpr int kCellsDown = 18;
    constexpr int kCells = kCellsAcross * kCellsDown;
    std::array<std::uint8_t, kCells> used{};
    std::array<int, kTribes> capitals{};
    const auto cell = [&](int cx, int cy) -> std::uint8_t& {
        return used[static_cast<std::size_t>(cx * kCellsDown + cy)];
    };
    int cellsUsed = 0;
    // A capital for each tribe, as far from the others as can be found.
    for (int t = 0; t < kTribes; ++t) {
        bool ok = false;
        int tries = 0;
        int x = 0;
        int y = 0;
        do {
            ++tries;
            x = random.between(8, w - 8);
            y = random.between(12, h - 12);
            if (flatWater(map, x, y) || (map.at(x, y).relief & 1) != 0) {
                continue;
            }
            int d = 0;
            nearestSettlement(save, x, y, d);
            if (d == 0 || 90 - (tries >> 2) > d) {
                continue;
            }
            if (d < 8 && (8 - d) * 1000 > tries) {
                continue;
            }
            // The first two tribes keep to the west.
            if (t < 2 && x * 8 > tries) {
                continue;
            }
            if (cell(x / 5, y / 5) != 0 && tries < 10000) {
                continue;
            }
            ok = true;
        } while (!ok && tries < 12000);
        if (!ok) {
            continue;
        }
        if (auto* r = tribeAt(save, t)) {
            r[0] = static_cast<std::uint8_t>(x);
            r[1] = static_cast<std::uint8_t>(y);
        }
        const int s = foundSettlement(save, rules, t + kEuropeanNations, x, y);
        if (s >= 0) {
            makeCapital(save, s);
        }
        ++capitals[static_cast<std::size_t>(t)];
        cell(x / 5, y / 5) = 1;
        ++cellsUsed;
    }
    // More settlements: from a tribe's capital, a random walk over the
    // 5 × 5 parts of the map to one not used yet, at most 2160 walks.
    std::vector<std::array<int, 2>> spots;
    for (int steps = 0; cellsUsed < kCells;) {
        if (8 * kCells <= steps || static_cast<int>(save.settlements.size()) >= kMaxSettlements) {
            break;
        }
        int t = 0;
        do {
            t = random.between(0, 7);
        } while (capitals[static_cast<std::size_t>(t)] == 0);
        const auto* r = tribeAt(save, t);
        int cx = r != nullptr ? r[0] / 5 : 0;
        int cy = r != nullptr ? r[1] / 5 : 0;
        bool ok = false;
        ++steps;
        do {
            const auto d = static_cast<std::size_t>(random.between(0, 7));
            cx += kDx[d];
            cy += kDy[d];
            if (cx < 0 || cx >= kCellsAcross || cy < 0 || cy >= kCellsDown) {
                break;
            }
            ok = cell(cx, cy) == 0;
        } while (!ok);
        if (!ok) {
            continue;
        }
        spots.clear();
        for (int y = cy * 5 + 1; y < cy * 5 + 4; ++y) {
            for (int x = cx * 5 + 1; x < cx * 5 + 4; ++x) {
                if (!settlementGround(map, x, y)) {
                    continue;
                }
                bool crowded = false;
                for (std::size_t j = 0; j < 9 && !crowded; ++j) {
                    crowded = (flatMask(map, x + kDx[j], y + kDy[j]) & (kHasUnit | kHasTown)) != 0;
                }
                if (!crowded) {
                    spots.push_back({x, y});
                }
            }
        }
        if (!spots.empty()) {
            const auto& spot = spots[static_cast<std::size_t>(random.between(0, static_cast<int>(spots.size()) - 1))];
            int d = 0;
            const int near = nearestSettlement(save, spot[0], spot[1], d);
            if (near >= 0) {
                foundSettlement(save, rules, save.settlements[static_cast<std::size_t>(near)].nation, spot[0], spot[1]);
            }
        }
        cell(cx, cy) = 1;
        ++cellsUsed;
    }
    // A brave for each, on its own land within two squares.
    for (std::size_t s = 0; s < save.settlements.size(); ++s) {
        const auto& home = save.settlements[s];
        const int region = map.at(home.x, home.y).region & 0x0F;
        bool ok = false;
        int x = 0;
        int y = 0;
        for (int tries = 0; !ok && tries < 100; ++tries) {
            x = home.x + random.between(-2, 2);
            y = home.y + random.between(-2, 2);
            ok = innerSquare(map, x, y);
            if (ok) {
                const auto& sq = map.at(x, y);
                ok = (sq.region & 0x0F) == region && !sq.isWater() && (sq.flags & (kHasUnit | kHasTown)) == 0;
            }
        }
        if (ok) {
            save.units.push_back(brave(x, y, home.nation, static_cast<int>(s)));
            map.at(x, y).flags |= kHasUnit;
        }
    }
    // A tribe holds silver for each mountain square near its settlements.
    for (const auto& s : save.settlements) {
        auto* r = tribeAt(save, s.nation - kEuropeanNations);
        if (r == nullptr) {
            continue;
        }
        for (int y = s.y - 2; y <= s.y + 2; ++y) {
            for (int x = s.x - 2; x <= s.x + 2; ++x) {
                if (innerSquare(map, x, y) && squareType(map, x, y) == 0x1B) {
                    const int silver = (r[12] | (r[13] << 8)) + r[2];
                    r[12] = static_cast<std::uint8_t>(silver);
                    r[13] = static_cast<std::uint8_t>(silver >> 8);
                }
            }
        }
    }
    (void)h;
}

void computeSiteValues(SaveMap& map, const Ruleset& rules) {
    // The @RESOURCE number of each resource, as the original's tables give.
    const auto resourceNumber = [](Resource r) {
        switch (r) {
        case Resource::DepletedSilver:
            return 0;
        case Resource::Oasis:
            return 1;
        case Resource::Grain:
            return 2;
        case Resource::Cotton:
            return 3;
        case Resource::Tobacco:
            return 4;
        case Resource::Sugar:
            return 5;
        case Resource::Minerals:
            return 6;
        case Resource::Fish:
            return 7;
        case Resource::Beaver:
            return 8;
        case Resource::Deer:
            return 9;
        case Resource::Timber:
            return 10;
        case Resource::Silver:
            return 12;
        case Resource::HillsOre:
            return 13;
        default:
            return -1;
        }
    };
    const auto value = [&](int t) {
        // The terrain rows: 0-23 as numbered, then arctic, ocean, sea lane,
        // mountains and hills.
        const auto row = static_cast<std::size_t>(t >= 16 && t < 24 ? t - 8 : t >= 24 ? t - 8 : t);
        return row < rules.terrain.size() ? rules.terrain[row].value : 0;
    };
    std::vector<std::uint8_t> out(map.squares.size(), 0);
    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            int total = 0;
            if (!innerSquare(map, x, y) || map.at(x, y).isWater()) {
                out[static_cast<std::size_t>(y * map.width + x)] = 0;
                continue;
            }
            for (int j = 0; j <= 20; ++j) {
                const int nx = x + (j < 20 ? kDx20[static_cast<std::size_t>(j)] : 0);
                const int ny = y + (j < 20 ? kDy20[static_cast<std::size_t>(j)] : 0);
                if (!innerSquare(map, nx, ny)) {
                    continue;
                }
                const int t = squareType(map, nx, ny);
                const int r = resourceNumber(resourceAt(map, nx, ny));
                int v = 0;
                if (r >= 0) {
                    v = static_cast<std::size_t>(r) < rules.resourceValues.size()
                            ? rules.resourceValues[static_cast<std::size_t>(r)]
                            : 0;
                } else if (t == kOcean) {
                    v = 2;
                    for (std::size_t d = 0; d < 8; ++d) {
                        if (!flatWater(map, nx + kDx[d], ny + kDy[d])) {
                            v += 2;
                        }
                    }
                    v >>= 2;
                } else {
                    v = value(t);
                }
                if ((map.at(nx, ny).relief & 2) != 0) {
                    ++v;
                }
                int weight = 2;
                if (j < 4) {
                    weight = 5;
                    if (t == kOcean) {
                        v = (v + 1) >> 1;
                    }
                }
                if (j < 8) {
                    weight += 2;
                }
                if (j < 12) {
                    ++weight;
                }
                if (j < 20) {
                    ++weight;
                }
                if (j == 20) {
                    weight = 4;
                }
                total += (weight * v) >> 1;
            }
            // Half as much away from the open sea.
            bool open = false;
            int bestRegion = -1;
            for (std::size_t d = 0; d < 8; ++d) {
                const int nx = x + kDx[d];
                const int ny = y + kDy[d];
                if (!innerSquare(map, nx, ny) || !map.at(nx, ny).isWater()) {
                    continue;
                }
                int region = map.at(nx, ny).region & 0x0F;
                if (region == 0) {
                    region = 16;
                }
                if (bestRegion < 0 || region < bestRegion) {
                    bestRegion = region;
                }
                open = true;
            }
            if (!open || bestRegion != 1) {
                total >>= 1;
            }
            const int t = squareType(map, x, y);
            if (t == 0x1B) {
                total = 0;
            } else if (t == 0x1C) {
                total >>= 1;
            }
            out[static_cast<std::size_t>(y * map.width + x)] = static_cast<std::uint8_t>(std::clamp(total / 10, 0, 15));
        }
    }
    for (std::size_t i = 0; i < out.size(); ++i) {
        map.squares[i].seen = out[i];
    }
}

NewWorld generateNewWorld(const NewWorldSettings& settings, int humanNation, OriginalRandom& random) {
    Generator g(settings, random);
    return g.run(humanNation);
}

} // namespace viceroy
