#pragma once

#include <game-contract-sdk/game_base.hpp>
#include <blackjack/card.hpp>

namespace blackjack {

using eosio::name;
using eosio::asset;
using bytes = std::vector<char>;
using eosio::checksum256;
using eosio::check;
using card_game::card;
using card_game::cards_t;
using game_sdk::param_t;

namespace param {
    const uint16_t min_ante = 0;
    const uint16_t max_ante = 1;
    const uint16_t max_payout = 2;
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

        uint64_t primary_key() const { return ses_id; }

        EOSLIB_SERIALIZE(bet_row, (ses_id)(ante))
    };

    enum game_state {
        require_bet,
        require_play,
        deal_one_card,
        stand,
        double_down,
        deal_cards,
    };

    struct [[eosio::table("state")]] state_row {
        uint64_t ses_id;
        uint16_t state;

        std::vector<card> player_cards;
        card dealer_card;

        bool has_hit = false;

        uint64_t primary_key() const { return ses_id; }

        EOSLIB_SERIALIZE(state_row, (ses_id)(state)(player_cards)(dealer_card)(has_hit))
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
    void check_bet(uint64_t ses_id, const param_t& ante_bet) const;
    param_t get_and_check(uint64_t ses_id, uint16_t param, const std::string& error_msg) const;

    void update_state(state_table::const_iterator itr, game_state new_state) {
        state.modify(itr, get_self(), [&](auto& row) {
            row.state = new_state;
        });
    }

    enum class outcome {
        player,
        dealer,
        draw,
        carry_on
    };


    // handlers
    std::tuple<outcome, cards_t, cards_t> handle_deal_cards(state_table::const_iterator itr, checksum256&& rand);
    std::tuple<outcome, card> handle_deal_one_card(state_table::const_iterator itr, checksum256&& rand);
    std::tuple<outcome, cards_t> handle_stand(state_table::const_iterator itr, checksum256&& rand);

    std::tuple<asset, std::vector<param_t>> on_stand(state_table::const_iterator state_itr, bet_table::const_iterator bet_itr, checksum256&& rand);

    card_game::labels_t prepare_deck(state_table::const_iterator state_itr, checksum256&& rand) {
    #ifdef IS_DEBUG
        auto debug_labels = debug_labels_singleton(_self, _self.value).get_or_default().labels;
        if (!debug_labels.empty()) {
            return debug_labels;
        }
    #endif
        auto labels = card_game::get_labels();
        for (const auto& c : state_itr->player_cards) {
            const auto it = std::find(labels.begin(), labels.end(), c.to_string());
            check(it != labels.end(), "invalid player card while preparing a deck");
            labels.erase(it);
        }
        if (state_itr->dealer_card) {
            const auto it = std::find(labels.begin(), labels.end(), state_itr->dealer_card.to_string());
            check(it != labels.end(), "invalid dealer card while preparing a deck");
            labels.erase(it);
        }
        service::shuffle(labels.begin(), labels.end(), get_prng(std::move(rand)));
        return labels;
    }

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
