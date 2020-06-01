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
    const uint16_t insure = 0;
    const uint16_t split = 1;
    const uint16_t double_down = 2;
}

class [[eosio::contract]] blackjack: public game_sdk::game {

public:
    struct [[eosio::table("bet")]] bet_row {
        uint64_t ses_id;
        asset ante;

        uint64_t primary_key() const { return ses_id; }

        EOSLIB_SERIALIZE(bet_row, (ses_id)(ante))
    };
    using bet_table = eosio::multi_index<"bet"_n, bet_row>;
public:
    blackjack(name receiver, name code, eosio::datastream<const char*> ds):
        game(receiver, code, ds),
        bets(_self, _self.value) {}

    void on_new_game(uint64_t ses_id) override final;
    void on_action(uint64_t ses_id, uint16_t type, std::vector<game_sdk::param_t> params) override final;
    void on_random(uint64_t ses_id, checksum256 rand) override final;
    void on_finish(uint64_t ses_id) override final;

    void check_params(uint64_t ses_id) const;
    void check_bet(uint64_t ses_id, const param_t& ante_bet) const;
    param_t get_and_check(uint64_t ses_id, uint16_t param, const std::string& error_msg) const;

private:
    bet_table bets;
};

} // ns blackjack
