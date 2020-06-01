#include <blackjack/blackjack.hpp>
#include <blackjack/card.hpp>
#include <game-contract-sdk/service.hpp>

namespace blackjack {

using card_game::card;

void blackjack::on_new_game(uint64_t ses_id) {
}

void blackjack::on_action(uint64_t ses_id, uint16_t type, std::vector<game_sdk::param_t> params) {
}

void blackjack::on_random(uint64_t ses_id, checksum256 rand) {
}

void blackjack::on_finish(uint64_t ses_id) {
}

} // namespace blackjack

GAME_CONTRACT(blackjack::blackjack)