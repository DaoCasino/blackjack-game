#pragma once

#include <vector>
#include <string>
#include <memory>
#include <ostream>

#ifndef TEST
#include <eosio/serialize.hpp>
#else
#include <fc/reflect/reflect.hpp>
#endif

namespace card_game {

using labels_t = std::vector<std::string>;

const std::vector<char> RANKS{'2', '3', '4', '5', '6', '7', '8', '9', 'T', 'J', 'Q', 'K', 'A'};
const std::vector<char> COLORS{'c', 'd', 'h', 's'};

static labels_t get_labels() {
    std::vector<std::string> card_labels;
    card_labels.reserve(52);
    for (const auto& rank : RANKS) {
        for (const auto& color : COLORS) {
            std::string label;
            label.push_back(rank);
            label.push_back(color);
            card_labels.push_back(label);
        }
    }
    return card_labels;
}

enum class rank;
enum class color;

// cards are compared by rank
struct card {
    card(): value(UNINITIALIZED) {}
    explicit card(int v): value(v) {}
    explicit card(const std::string& card) {
        static const auto& labels = get_labels();
        value = std::find(labels.begin(), labels.end(), card) - labels.begin();
        #ifndef TEST
        eosio::check(value < 52, "invalid card: " + card);
        #endif
    }
    card(const char s[3]) {
        static const auto& labels = get_labels();
        std::string card;
        card.push_back(s[0]);
        card.push_back(s[1]);
        value = std::find(labels.begin(), labels.end(), card) - labels.begin();
        #ifndef TEST
        eosio::check(value < 52, "invalid card: " + std::string(s));
        #endif
    }

    bool operator<(const card& c) const {
        return get_rank() < c.get_rank();
    }

    bool operator>(const card& c) const {
        return get_rank() > c.get_rank();
    }

    bool operator==(const card& c) const {
        return value == c.value;
    }

    operator bool() const {
        return 0 <= value && value < 52;
    }

    rank get_rank() const { return rank(value / 4); }

    color get_color() const { return color(value % 4); }

    rank next_rank() const { return rank(value / 4 + 1); }

    std::string to_string() const {
        std::string r;
        r.push_back(RANKS.at(static_cast<int>(get_rank())));
        r.push_back(COLORS.at(static_cast<int>(get_color())));
        return r;
    }

    unsigned get_value() const {
        return value;
    }

    static constexpr int UNINITIALIZED = 100;
private:
    unsigned value;

#ifndef TEST
    EOSLIB_SERIALIZE(card, (value))
#else
    friend struct fc::reflector<card>;
#endif
};

enum class rank {
    TWO,
    THREE,
    FOUR,
    FIVE,
    SIX,
    SEVEN,
    EIGHT,
    NINE,
    TEN,
    JACK,
    QUEEN,
    KING,
    ACE
};

enum class color {
    CLUB,
    DIAMOND,
    HEART,
    SPADE
};

using cards_t = std::vector<card>;

static int get_weight(const card& c) {
    if (c.get_rank() == rank::ACE) {
        return 1;
    } else if (c.get_rank() < rank::TEN) {
        return static_cast<int>(c.get_rank()) + 2;
    }
    return 10;
}

static int get_weight(const cards_t& cards) {
    int aces = 0, w = 0;
    for (const card& c : cards) {
        if (c.get_rank() == rank::ACE) {
            aces++;
        }
        w += get_weight(c);
    }
    // maximum possible weight less that 21
    while (aces && w + 10 <= 21) {
        w += 10;
        --aces;
    }
    return w;
}

bool is_hard(const cards_t& cards) {
    int aces = 0, w = 0;
    for (const auto& c : cards) {
        if (c.get_rank() == card_game::rank::ACE) {
            aces++;
        }
        w += card_game::get_weight(c);
    }
    // A hand without an ace hand in which all the aces have a value of 1, is known as a “hard hand”
    return !aces || w + 10 > 21;
}

static std::ostream& operator<<(std::ostream& os, const card& c) {
    os << c.to_string();
    return os;
}

static std::ostream& operator<<(std::ostream& os, const cards_t& cards) {
    os << "{";
    for (int i = 0; i < static_cast<int>(cards.size()) - 1; i++) {
        os << cards[i] << ", ";
    }
    if (!cards.empty()) {
        os << cards.back();
    }
    os << "}";
    return os;
}

enum class combination : uint8_t {
    HIGH_CARD = 0,
    PAIR,
    FLUSH,
    STRAIGHT,
    STRAIGHT_FLUSH,
    THREE_OF_A_KIND,
    SUITED_THREE_OF_A_KIND
};

combination get_combination(const cards_t& cards_) {
    auto cards = cards_;
    std::sort(std::begin(cards), std::end(cards), [](const card& c1, const card& c2) {
        return c1.get_rank() > c2.get_rank();
    });
    if (cards[0].get_rank() == cards[1].get_rank() && cards[1].get_rank() == cards[2].get_rank()) {
        if (cards[0].get_color() == cards[1].get_color() && cards[1].get_color() == cards[2].get_color()) {
            return combination::SUITED_THREE_OF_A_KIND;
        }
        return combination::THREE_OF_A_KIND;
    }
    bool flush = (cards[0].get_color() == cards[1].get_color() && cards[1].get_color() == cards[2].get_color());
    bool straight = false;

    // special case A 3 2 - straight
    if (cards[0].get_rank() == rank::ACE && cards[1].get_rank() == rank::THREE && cards[2].get_rank() == rank::TWO) {
        straight = true;
    }

    if (cards[0].get_rank() == cards[1].next_rank() && cards[1].get_rank() == cards[2].next_rank()) {
        straight = true;
    }

    if (straight && flush) {
        return combination::STRAIGHT_FLUSH;
    } else if (straight) {
        return combination::STRAIGHT;
    } else if (flush) {
        return combination::FLUSH;
    }

    if (cards[0].get_rank() == cards[1].get_rank() || cards[1].get_rank() == cards[2].get_rank()) {
        return combination::PAIR;
    }
    return combination::HIGH_CARD;
}

} // ns card_game

#ifdef TEST
FC_REFLECT(card_game::card, (value))
#endif