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
    check(3 * ante / 2 == get_session(ses_id).deposit.amount, "max loss is more than deposit");
}

std::tuple<blackjack::outcome, cards_t, cards_t> blackjack::deal_initial_cards(state_table::const_iterator state_itr, checksum256&& rand) {
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

std::tuple<blackjack::outcome, card> blackjack::deal_a_card(state_table::const_iterator state_itr, checksum256&& rand) {
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

std::tuple<blackjack::outcome, bool> blackjack::compare_cards(const cards_t& player_cards, const cards_t& dealer_cards) {
    const int player_weight = card_game::get_weight(player_cards);
    const int dealer_weight = card_game::get_weight(dealer_cards);
    // If both the dealer and player bust, the player loses.
    const bool player_has_a_blackjack = (player_cards.size() == 2 && player_weight == 21);
    const bool dealer_has_a_blackjack = (dealer_cards.size() == 2 && dealer_weight == 21);
    if (player_weight > 21) {
        return std::make_tuple(outcome::dealer, dealer_has_a_blackjack);
    }
    if (dealer_weight > 21) {
        return std::make_tuple(outcome::player, player_has_a_blackjack);
    }
    if (player_weight < dealer_weight) {
        return std::make_tuple(outcome::dealer, dealer_has_a_blackjack);
    } else if (player_weight == dealer_weight) {
        if (!player_has_a_blackjack && dealer_has_a_blackjack) {
            return std::make_tuple(outcome::dealer, dealer_has_a_blackjack);
        } else if (player_has_a_blackjack && !dealer_has_a_blackjack) {
            return std::make_tuple(outcome::player, player_has_a_blackjack);
        }
        return std::make_tuple(outcome::draw, false);
    }
    return std::make_tuple(outcome::player, player_has_a_blackjack);
}

cards_t blackjack::open_dealer_cards(state_table::const_iterator state_itr, checksum256&& rand) {
    cards_t dealer_cards{state_itr->dealer_card};
    auto deck = prepare_deck(state_itr, std::move(rand));
    int i = 0;
    // dealer should stand on soft 17
    while (card_game::get_weight(dealer_cards) <= 16) {
        dealer_cards.push_back(card(deck.at(i++)));
    }
    return dealer_cards;
}

asset blackjack::get_win(asset ante, outcome result, bool has_blackjack) {
    check(result != outcome::carry_on, "invariant check failed: invalid outcome");
    if (result == outcome::draw) {
        return zero_asset;
    }
    if (result == outcome::player) {
        if (has_blackjack) {
            return 3 * ante / 2;
        }
        return ante;
    }
    if (has_blackjack) {
        return -3 * ante / 2;
    }
    return -ante;
}

std::tuple<asset, std::vector<param_t>> blackjack::compare_and_finish(state_table::const_iterator state_itr, asset ante, checksum256&& rand) {
    // returns players win & dealer's cards
    auto dealer_cards = open_dealer_cards(state_itr, std::move(rand));
    auto [res, bjack] = compare_cards(state_itr->player_cards, dealer_cards);
    auto player_win = get_win(ante, res, bjack);
    eosio::print_f("player's 1st round win: %s\n", player_win.to_string());
    if (state_itr->has_split()) {
        std::tie(res, bjack) = compare_cards(state_itr->split_cards, dealer_cards);
        const auto split_win = get_win(state_itr->first_round_ante, res, bjack);
        player_win += split_win;
        eosio::print_f("player's 2nd round win: %s\n", split_win.to_string());
    }
    // the first card isn't new. it's been dealt at the begining
    dealer_cards.erase(dealer_cards.begin());
    std::vector<param_t> cards;
    for (const auto& c : dealer_cards) { cards.push_back(c.get_value()); }
    return std::make_tuple(player_win, std::move(cards));
}

void blackjack::check_deposit(asset deposit, asset current_ante, asset prev_round_ante) {
    eosio::print_f("deposit: %s, current ante: %s, prev round ante: %s\n", deposit, current_ante, prev_round_ante);
    check(deposit == 3 * current_ante / 2 + 3 * prev_round_ante / 2, "invalid deposit");
}

void blackjack::on_new_game(uint64_t ses_id) {
    check_params(ses_id);
    require_action(action::bet);
    state.emplace(get_self(), [&](auto& row) {
        row.ses_id = ses_id;
        row.state = game_state::require_bet;
        row.first_round_ante = zero_asset;
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
        update_max_win(2 * get_session(ses_id).deposit);
        update_state(state_itr, game_state::deal_cards);
    } else if (type == action::play) {
        check(state_itr->state == game_state::require_play, "game state should be require_play");
        check(params.size() == 1, "invalid param size");
        const auto ante = bet.require_find(ses_id, "no ante bet")->ante;
        switch (params[0]) {
            case decision::hit:
                update_state(state_itr, game_state::deal_one_card);
                break;
            case decision::stand:
                // if it's a first round and player has splitted then just save the cards
                if (state_itr->has_split() && !state_itr->second_round) {
                    eosio::print("player stands and swaps active cards");
                    finish_first_round(state_itr);
                    return;
                }
                update_state(state_itr, game_state::stand);
                break;
            case decision::split:
                check(!state_itr->has_split(), "cannot split again");
                check(state_itr->player_cards.size() == 2, "cannot split");
                check(state_itr->player_cards[0].get_rank() == state_itr->player_cards[1].get_rank(), "cannot split non-pair");
                check_deposit(get_session(ses_id).deposit, 2 * ante, zero_asset);
                // split cards
                state.modify(state_itr, get_self(), [&](auto& row) {
                    row.split_cards.push_back(row.player_cards.back());
                    row.player_cards.pop_back();
                    row.first_round_ante = ante;
                });
                update_max_win(2 * get_session(ses_id).deposit);
                update_state(state_itr, game_state::split);
                break;
            case decision::double_down: {
                check(!state_itr->has_hit(), "player's already hit");
                check(!state_itr->player_cards.empty(), "cards have not been dealt yet");
                const auto& cards = state_itr->player_cards;
                // https://wizardofodds.com/games/blackjack/strategy/european/
                const auto w = card_game::get_weight(cards);
                const auto hard = card_game::is_hard(cards);
                check(9 <= w && w <= 11 && hard, "player may only double on hard totals of 9-11");
                check_deposit(get_session(ses_id).deposit, ante * 2, state_itr->first_round_ante);
                update_max_win(2 * get_session(ses_id).deposit);
                update_state(state_itr, game_state::double_down);
                break;
            }
            default:
                check(0, "invalid decision");
        }
    } else {
        check(0, "invalid action");
    }
    // random for next card(s)
    require_random();
}

void blackjack::on_random(uint64_t ses_id, checksum256 rand) {
    const auto state_itr = state.require_find(ses_id, "invalid ses_id");
    const auto ante = bet.require_find(ses_id, "invalid ses_id")->ante;

    switch (state_itr->state) {
        case game_state::deal_cards: {
            eosio::print("dealing cards");
            const auto [res, player_cards, dealer_cards] = deal_initial_cards(state_itr, std::move(rand));
            std::vector<param_t> cards;
            for (const auto& c : player_cards) { cards.push_back(c.get_value()); }
            for (const auto& c : dealer_cards) { cards.push_back(c.get_value()); }

            if (res == outcome::draw) {
                // both players have a blackjack
                eosio::print("both dealer and player get a blackjack");
                finish_game(get_session(ses_id).deposit, std::move(cards));
                return;
            } else if (res == outcome::player) {
                // player has a blackjack. It pays 3:2
                eosio::print("player gets a blackjack");
                finish_game(get_session(ses_id).deposit + 3 * ante / 2, std::move(cards));
                return;
            }
            update_state(state_itr, game_state::require_play);
            require_action(action::play);
            send_game_message(std::move(cards));
            break;
        }
        case game_state::deal_one_card: {
            eosio::print("player hits");
            const auto [res, player_card] = deal_a_card(state_itr, std::move(rand));
            send_game_message(std::vector<param_t>{player_card.get_value()});
            if (res == outcome::dealer) {
                // players busts
                if (!state_itr->has_split() || state_itr->second_round) {
                    const auto [win, dealer_cards] = compare_and_finish(state_itr, ante, std::move(rand));
                    finish_game(get_session(ses_id).deposit + win, std::move(dealer_cards));
                    return;
                } else {
                    finish_first_round(state_itr);
                }
            }
            update_state(state_itr, game_state::require_play);
            require_action(action::play);
            break;
        }
        case game_state::double_down: {
            eosio::print("player doubles down");
            const auto [res, player_card] = deal_a_card(state_itr, std::move(rand));
            check(res == outcome::carry_on, "invariant check failed: player cannot bust when doubling");
            send_game_message(std::vector<param_t>{player_card.get_value()});
            if (!state_itr->has_split() || state_itr->second_round) {
                const auto [win, dealer_cards] = compare_and_finish(state_itr, ante * 2, std::move(rand));
                finish_game(get_session(ses_id).deposit + win, std::move(dealer_cards));
                return;
            } else {
                state.modify(state_itr, get_self(), [&](auto& row) {
                    row.first_round_ante *= 2;
                });
                finish_first_round(state_itr);
            }
            update_state(state_itr, game_state::require_play);
            require_action(action::play);
            break;
        }
        case game_state::stand: {
            const auto [win, dealer_cards] = compare_and_finish(state_itr, ante, std::move(rand));
            finish_game(get_session(ses_id).deposit + win, std::move(dealer_cards));
            break;
        }
        case game_state::split: {
            eosio::print("player splits");
            // take 2 cards from the deck and send them to frontend
            const auto deck = prepare_deck(state_itr, std::move(rand));
            const auto ncard1 = card(deck[0]), ncard2 = card(deck[1]);
            state.modify(state_itr, get_self(), [&](auto& row) {
                row.player_cards.push_back(ncard1);
                row.split_cards.push_back(ncard2);
            });
            update_state(state_itr, game_state::require_play);
            require_action(action::play);
            send_game_message(std::vector<param_t>{
                ncard1.get_value(),
                ncard2.get_value()
            });
            break;
        }
        default:
            check(0, "invalid game state");
    }
}

void blackjack::on_finish(uint64_t ses_id) {
    state.erase(state.require_find(ses_id, "no ses_id in state"));
    bet.erase(bet.require_find(ses_id, "no ses_id in bet"));
}

} // namespace blackjack

#ifndef IS_DEBUG
GAME_CONTRACT(blackjack::blackjack)
#else
GAME_CONTRACT_CUSTOM_ACTIONS(blackjack::blackjack, (pushlabels))
#endif
