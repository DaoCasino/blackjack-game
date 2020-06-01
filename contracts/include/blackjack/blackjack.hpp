#pragma once

#include <game-contract-sdk/game_base.hpp>
#include <blackjack/card.hpp>

namespace blackjack {

using eosio::checksum256;
using eosio::name;

class [[eosio::contract]] blackjack: public game_sdk::game {
public:
    blackjack(name receiver, name code, eosio::datastream<const char*> ds):
        game(receiver, code, ds) {}

    void on_new_game(uint64_t ses_id) override final;
    void on_action(uint64_t ses_id, uint16_t type, std::vector<game_sdk::param_t> params) override final;
    void on_random(uint64_t ses_id, checksum256 rand) override final;
    void on_finish(uint64_t ses_id) override final;

};

} // ns blackjack
