#include <blackjack/blackjack.hpp>
#include <blackjack/card.hpp>
#include <game-contract-sdk/service.hpp>

namespace blackjack {

param_t blackjack::get_and_check(uint64_t ses_id, uint16_t param, const std::string& error_msg) const {
    const auto res = get_param_value(ses_id, param);
    if (res != std::nullopt) {
        return *res;
    }
    eosio::check(false, error_msg);
    return 0;
}

void blackjack::check_params(uint64_t ses_id) const {
    const auto min_ante_bet = get_and_check(ses_id, param::min_ante, "min ante bet is absent");
    const auto max_ante_bet = get_and_check(ses_id, param::max_ante, "max ante bet is absent");
    const auto max_payout = get_and_check(ses_id, param::max_payout, "max payout is absent");
    check(max_ante_bet >= min_ante_bet, "max ante bet is less than min");
    check(min_ante_bet <= get_session(ses_id).deposit.amount, "deposit is less than min bet");
    check(max_payout >= get_session(ses_id).deposit.amount, "deposit exceeds max payout");
}

void blackjack::check_bet(uint64_t ses_id, const param_t& ante) const {
    check(*get_param_value(ses_id, param::min_ante) <= ante, "ante bet is less than min");
    check(*get_param_value(ses_id, param::max_ante) >= ante, "ante bet is more than max");
    check(2 * ante == get_session(ses_id).deposit.amount, "max loss is more than deposit");
}

void blackjack::on_new_game(uint64_t ses_id) {
    check_params(ses_id);
    require_action(action::bet);
}

void blackjack::on_action(uint64_t ses_id, uint16_t type, std::vector<game_sdk::param_t> params) {
    if (type == action::bet) {
        check(params.size() == 1, "invalid param size");
        check_bet(ses_id, params[0]);

        bets.emplace(get_self(), [&](auto& row) {
            row.ses_id = ses_id;
            row.ante = asset(params[0], core_symbol);
        });
        // generate initial cards
        require_random();
    } else if (type == action::play) {
        check(params.size() == 1, "invalid param size");
        switch (params[0]) {
            case decision::insure:
                return;
            case decision::split:
                return;
            case decision::double_down:
                return;
            default:
                check(0, "invalid decision");
        }
    } else {
        check(0, "invalid action");
    }
}

void blackjack::on_random(uint64_t ses_id, checksum256 rand) {
    // STUB
    require_action(action::play);
}

void blackjack::on_finish(uint64_t ses_id) {
}

} // namespace blackjack

GAME_CONTRACT(blackjack::blackjack)