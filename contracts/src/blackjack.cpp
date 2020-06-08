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

std::tuple<blackjack::outcome, cards_t, cards_t> blackjack::handle_deal_cards(state_table::const_iterator state_itr, checksum256&& rand) {
    auto deck = prepare_deck(state_itr, std::move(rand));
    const cards_t player_cards{card(deck[0]), card(deck[1])};
    const auto open_card = card(deck[2]);

    if (card_game::get_weight(player_cards) == 21) {
        // player hits a blackjack at the start of the game
        const auto hole_card = card(deck[3]);
        const cards_t dealer_cards{open_card, hole_card};
        if (card_game::get_weight(dealer_cards) == 21) {
            return std::make_tuple(outcome::draw, player_cards, dealer_cards);
        }
        return std::make_tuple(outcome::player, player_cards, dealer_cards);
    }

    // hole card returns to the deck
    state.modify(state_itr, get_self(), [&](auto& row) {
        row.player_cards = player_cards;
        row.dealer_card = open_card;
    });
    return std::make_tuple(outcome::carry_on, player_cards, cards_t{open_card});
}

std::tuple<blackjack::outcome, card> blackjack::handle_deal_one_card(state_table::const_iterator state_itr, checksum256&& rand) {
    const auto deck = prepare_deck(state_itr, std::move(rand));
    const auto new_card = card(deck[0]);
    auto player_cards = state_itr->player_cards;
    player_cards.push_back(new_card);

    state.modify(state_itr, get_self(), [&](auto& row) {
        row.player_cards = player_cards;
    });

    if (card_game::get_weight(player_cards) > 21) {
        // player gets busted
        return std::make_tuple(outcome::dealer, new_card);
    }
    return std::make_tuple(outcome::carry_on, new_card);
}

std::tuple<blackjack::outcome, cards_t> blackjack::handle_finish_game(state_table::const_iterator state_itr, checksum256&& rand) {
    auto deck = prepare_deck(state_itr, std::move(rand));
    cards_t dealer_cards{state_itr->dealer_card};

    int i = 0;
    // dealer should stand on soft 17
    while (card_game::get_weight(dealer_cards) <= 16) {
        dealer_cards.push_back(card(deck.at(i++)));
    }
    int dealer_weight = card_game::get_weight(dealer_cards);
    if (dealer_weight > 21) {
        // busted
        return std::make_tuple(outcome::player, dealer_cards);
    }
    int player_weight = card_game::get_weight(state_itr->player_cards);
    if (player_weight < dealer_weight) {
        return std::make_tuple(outcome::dealer, dealer_cards);
    } else if (player_weight == dealer_weight) {
        return std::make_tuple(outcome::draw, dealer_cards);
    }
    return std::make_tuple(outcome::player, dealer_cards);
}

void blackjack::on_new_game(uint64_t ses_id) {
    check_params(ses_id);
    require_action(action::bet);
    state.emplace(get_self(), [&](auto& row) {
        row.ses_id = ses_id;
        row.state = game_state::require_bet;
    });
}

void blackjack::on_action(uint64_t ses_id, uint16_t type, std::vector<game_sdk::param_t> params) {
    const auto state_itr = state.require_find(ses_id, "invalid ses_id");
    if (type == action::bet) {
        check(state_itr->state == game_state::require_bet, "game state should be require_bet");
        check(params.size() == 1, "invalid param size");
        check_bet(ses_id, params[0]);
        bet.emplace(get_self(), [&](auto& row) {
            row.ses_id = ses_id;
            row.ante = asset(params[0], core_symbol);
        });

        update_max_win(get_session(ses_id).deposit * 2);
        update_state(state_itr, game_state::deal_cards);
        // generate initial cards
        require_random();
    } else if (type == action::play) {
        check(state_itr->state == game_state::require_play, "game state should be require_play");
        check(params.size() == 1, "invalid param size");
        switch (params[0]) {
            case decision::hit:
                update_state(state_itr, game_state::deal_one_card);
                break;
            case decision::stand:
                update_state(state_itr, game_state::finish);
                break;
            case decision::insure:
                // TODO
                break;
            case decision::split:
                // TODO
                break;
            case decision::double_down:
                // TODO
                break;
            default:
                check(0, "invalid decision");
        }
        // random for next card(s)
        require_random();
    } else {
        check(0, "invalid action");
    }
}

void blackjack::on_random(uint64_t ses_id, checksum256 rand) {
    const auto state_itr = state.require_find(ses_id, "invalid ses_id");
    switch (state_itr->state) {
        case game_state::deal_cards: {
            const auto [res, player_cards, dealer_cards] = handle_deal_cards(state_itr, std::move(rand));
            std::vector<param_t> cards;
            for (const auto& c : player_cards) { cards.push_back(c.get_value()); }
            for (const auto& c : dealer_cards) { cards.push_back(c.get_value()); }

            if (res == outcome::draw) {
                // both players have a blackjack
                finish_game(get_session(ses_id).deposit, std::move(cards));
                return;
            } else if (res == outcome::player) {
                // player has a blackjack. It pays 3:2
                finish_game(get_session(ses_id).deposit + 3 * bet.get(ses_id).ante / 2, std::move(cards));
                return;
            }
            update_state(state_itr, game_state::require_play);
            require_action(action::play);
            send_game_message(std::move(cards));
            break;
        }
        case game_state::deal_one_card: {
            const auto [res, player_card] = handle_deal_one_card(state_itr, std::move(rand));
            if (res == outcome::dealer) {
                // players busts
                finish_game(get_session(ses_id).deposit - bet.get(ses_id).ante, std::vector<param_t>{player_card.get_value()});
            } else {
                // game continues
                update_state(state_itr, game_state::require_play);
                require_action(action::play);
                send_game_message(std::vector<param_t>{player_card.get_value()});
            }
            break;
        }
        case game_state::finish: {
            auto [res, dealer_cards] = handle_finish_game(state_itr, std::move(rand));
            auto player_win = zero_asset;
            const auto ante = bet.get(ses_id).ante;

            if (res == outcome::player) {
                player_win = ante;
            } else if (res == outcome::dealer) {
                if (dealer_cards.size() == 2 && card_game::get_weight(dealer_cards) == 21) {
                    // dealer has a blackjack
                    player_win = -3 * ante / 2;
                } else {
                    player_win = -ante;
                }
            }

            // first card isn't new. it's been dealt at the begining
            dealer_cards.erase(dealer_cards.begin());
            std::vector<param_t> cards;
            for (const auto& c : dealer_cards) { cards.push_back(c.get_value()); }
            finish_game(get_session(ses_id).deposit + player_win, std::move(cards));
            break;
        }
        default:
            check(0, "invalid game state");
    }
}

void blackjack::on_finish(uint64_t ses_id) {
}

} // namespace blackjack

#ifndef IS_DEBUG
GAME_CONTRACT(blackjack::blackjack)
#else
GAME_CONTRACT_CUSTOM_ACTIONS(blackjack::blackjack, (pushlabels))
#endif
