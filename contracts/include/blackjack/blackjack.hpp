#pragma once

#include <game-contract-sdk/game_base.hpp>
#include <blackjack/card.hpp>

namespace blackjack {

using eosio::name;
using eosio::asset;
using bytes = std::vector<char>;
using eosio::checksum256;
using eosio::check;
using card_game::labels_t;
using card_game::card;
using card_game::cards_t;
using card_game::combination;
using card_game::get_combination;
using game_sdk::param_t;

namespace param {
    const uint16_t min_ante = 0;
    const uint16_t max_ante = 1;
    const uint16_t max_payout = 2;
    const uint16_t max_pair = 3;
    const uint16_t max_first_three = 4;
}

namespace action {
    const uint16_t bet = 0;
    const uint16_t play = 1;
}

namespace decision {
    const uint16_t hit = 0;
    const uint16_t stand = 1;
    const uint16_t split = 2;
    const uint16_t double_down = 3;
}

class [[eosio::contract]] blackjack: public game_sdk::game {

public:
    struct [[eosio::table("bet")]] bet_row {
        uint64_t ses_id;
        asset ante;
        // side bets
        asset pair;
        asset first_three;

        asset side_bets_sum() const {
            return pair + first_three;
        }

        uint64_t primary_key() const { return ses_id; }

        EOSLIB_SERIALIZE(bet_row, (ses_id)(ante)(pair)(first_three))
    };

    enum game_state {
        require_bet,
        require_play,
        deal_one_card,
        stand,
        double_down,
        split,
        deal_cards,
    };

    struct [[eosio::table("state")]] state_row {
        uint64_t ses_id;
        uint16_t state;

        cards_t active_cards;
        card dealer_card;

        // need this to handle split action
        cards_t split_cards;
        asset first_round_ante;
        bool second_round = false;

        // side bets
        asset pair_win;
        asset first_three_win;

        // methods
        bool has_hit() const {
            return active_cards.size() > 2;
        }
        bool has_split() const {
            return !split_cards.empty();
        }
        uint64_t primary_key() const { return ses_id; }

        EOSLIB_SERIALIZE(state_row, (ses_id)(state)(active_cards)(dealer_card)(split_cards)(first_round_ante)(second_round)(pair_win)(first_three_win))
    };

    using bet_table = eosio::multi_index<"bet"_n, bet_row>;
    using state_table = eosio::multi_index<"state"_n, state_row>;
public:
    blackjack(name receiver, name code, eosio::datastream<const char*> ds):
        game(receiver, code, ds),
        bet(_self, _self.value),
        state(_self, _self.value) {}

    void on_new_game(uint64_t ses_id) override final;
    void on_action(uint64_t ses_id, uint16_t type, std::vector<game_sdk::param_t> params) override final;
    void on_random(uint64_t ses_id, checksum256 rand) override final;
    void on_finish(uint64_t ses_id) override final;

    void check_params(uint64_t ses_id) const;
    void check_bet(uint64_t ses_id, const param_t& ante_bet, const param_t& pair, const param_t& first_three) const;
    param_t get_and_check(uint64_t ses_id, uint16_t param, const std::string& error_msg) const;

    void validate_new_state(game_state current_state, game_state new_state) {
        switch(new_state) {
            case game_state::require_bet:
                check(0, "cannot update state to require_bet");
            case game_state::require_play:
                check(current_state == game_state::deal_one_card ||
                      current_state == game_state::double_down ||
                      current_state == game_state::split ||
                      current_state == game_state::deal_cards, "cannot update state to require_play");
                break;
            case game_state::deal_one_card:
            case game_state::stand:
            case game_state::double_down:
            case game_state::split:
                check(current_state == game_state::require_play, "state should be require_play");
                break;
            case game_state::deal_cards:
                check(current_state == game_state::require_bet, "state should be require_bet");
                break;
            default:
                check(0, "unknown new_state");
        }
    }

    void update_state(state_table::const_iterator state_itr, game_state new_state) {
        validate_new_state(game_state(state_itr->state), new_state);
        state.modify(state_itr, get_self(), [&](auto& row) {
            row.state = new_state;
        });
    }

    enum class outcome {
        player,
        dealer,
        draw,
        carry_on
    };

    std::tuple<outcome, cards_t, cards_t> deal_initial_cards(state_table::const_iterator itr, const checksum256& rand);

    std::tuple<outcome, card, labels_t> deal_a_card(state_table::const_iterator itr, const checksum256& rand);

    std::tuple<asset, cards_t> compare_and_finish(state_table::const_iterator state_itr, asset ante, const checksum256& rand, labels_t&& deck);

    cards_t open_dealer_cards(state_table::const_iterator state_itr, const checksum256& rand, labels_t& deck);

    asset get_win(asset ante, outcome result, bool has_blackjack);

    std::tuple<outcome, bool> compare_cards(const cards_t& active_cards, const cards_t& dealer_cards, bool has_split);

    std::vector<param_t> encode_cards(cards_t&& dealer_cards, cards_t&& player_cards = {}) {
        std::vector<param_t> result;
        result.reserve(2 + player_cards.size() + dealer_cards.size());
        result.push_back(player_cards.size());
        for (const auto& c : player_cards) {
            result.push_back(c.get_value());
        }
        result.push_back(dealer_cards.size());
        for (const auto& c : dealer_cards) {
            result.push_back(c.get_value());
        }
        return result;
    }

    void end_game(asset payout, cards_t&& dealer_cards, cards_t&& player_cards = {}) {
        finish_game(payout, encode_cards(std::move(dealer_cards), std::move(player_cards)));
    }

    void clean_labels(card_game::labels_t& labels, state_table::const_iterator state_itr) {
        // remove cards from the deck that are in the game
        for (const auto& c : state_itr->active_cards) {
            const auto it = std::find(labels.begin(), labels.end(), c.to_string());
            if (it != labels.end()) {
                labels.erase(it);
            }
        }
        for (const auto& c : state_itr->split_cards) {
            const auto it = std::find(labels.begin(), labels.end(), c.to_string());
            if (it != labels.end()) {
                labels.erase(it);
            }
        }
        if (state_itr->dealer_card) {
            const auto it = std::find(labels.begin(), labels.end(), state_itr->dealer_card.to_string());
            if (it != labels.end()) {
                labels.erase(it);
            }
        }
    }

    card_game::labels_t prepare_deck(state_table::const_iterator state_itr, checksum256 rand) {
    #ifdef IS_DEBUG
        auto debug_labels = debug_labels_singleton(_self, _self.value).get_or_default().labels;
        clean_labels(debug_labels, state_itr);
        if (!debug_labels.empty()) {
            return debug_labels;
        }
    #endif
        auto labels = card_game::get_labels();
        // 8 deck blackjack
        card_game::labels_t multideck;
        multideck.reserve(8 * labels.size());
        for (const auto& label : labels) {
            for (int i = 0; i < 8; i++) {
                multideck.push_back(label);
            }
        }
        // remove player's cards
        clean_labels(multideck, state_itr);
        // draw 9 cards
        card_game::labels_t result(9);
        auto prng = get_prng(std::move(rand));
        for (int i = 0; i < 9; i++) {
            const auto idx = prng->next() % multideck.size();
            result[i] = multideck[idx];
            multideck.erase(multideck.begin() + idx);
        }
        return result;
    }

    void finish_first_round(state_table::const_iterator state_itr) {
        eosio::print("first round's finished\n");
        state.modify(state_itr, get_self(), [&](auto& row) {
            row.second_round = true;
            // now the split cards become active
            std::swap(row.active_cards, row.split_cards);
        });
    }

    void check_deposit(asset deposit, asset current_ante, asset prev_round_ante, asset side_bets_sum);

#ifdef IS_DEBUG
    struct [[eosio::table("labelsdeb")]] labels_deb {
        card_game::labels_t labels;
    };

    using debug_labels_singleton = eosio::singleton<"labelsdeb"_n, labels_deb>;

    [[eosio::action("pushlabels")]]
    void pushlabels(uint64_t ses_id, card_game::labels_t labels) {
        debug_labels_singleton(_self, _self.value).set(labels_deb{labels}, _self);
    }
#endif

private:
    bet_table bet;
    state_table state;
};

} // ns blackjack
