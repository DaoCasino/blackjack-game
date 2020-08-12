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

void blackjack::check_bet(uint64_t ses_id, const param_t& ante, const param_t& pair, const param_t& first_three) const {
    check(*get_param_value(ses_id, param::min_ante) <= ante, "ante bet is less than min");
    check(*get_param_value(ses_id, param::max_ante) >= ante, "ante bet is more than max");
    check(get_and_check(ses_id, param::max_pair, "max pair is absent") >= pair, "pair bet is more than max");
    check(get_and_check(ses_id, param::max_first_three, "max first three is absent") >= first_three, "first three bet is more than max");
    check(ante + pair + first_three == get_session(ses_id).deposit.amount, "max loss is more than deposit");
}

std::tuple<blackjack::outcome, cards_t, cards_t> blackjack::deal_initial_cards(state_table::const_iterator state_itr, const checksum256& rand) {
    const auto deck = prepare_deck(state_itr, rand);
    const cards_t active_cards{card(deck[0]), card(deck[1])};
    const auto open_card = card(deck[2]);

    if (card_game::get_weight(active_cards) == 21) {
        // player hits a blackjack at the start of the game
        const auto hole_card = card(deck[3]);
        const cards_t dealer_cards{open_card, hole_card};
        if (card_game::get_weight(dealer_cards) == 21) {
            return std::make_tuple(outcome::draw, active_cards, dealer_cards);
        }
        return std::make_tuple(outcome::player, active_cards, dealer_cards);
    }

    // hole card returns to the deck
    state.modify(state_itr, get_self(), [&](auto& row) {
        row.active_cards = active_cards;
        row.dealer_card = open_card;
    });
    return std::make_tuple(outcome::carry_on, active_cards, cards_t{open_card});
}

std::tuple<blackjack::outcome, card, labels_t> blackjack::deal_a_card(state_table::const_iterator state_itr, const checksum256& rand) {
    auto deck = prepare_deck(state_itr, rand);
    const auto new_card = card(*deck.begin());
    deck.erase(deck.begin());
    auto active_cards = state_itr->active_cards;
    active_cards.push_back(new_card);

    state.modify(state_itr, get_self(), [&](auto& row) {
        row.active_cards = active_cards;
    });

    if (card_game::get_weight(active_cards) > 21) {
        // player gets busted
        return std::make_tuple(outcome::dealer, new_card, deck);
    }
    return std::make_tuple(outcome::carry_on, new_card, deck);
}

std::tuple<blackjack::outcome, bool> blackjack::compare_cards(const cards_t& active_cards, const cards_t& dealer_cards, bool has_split) {
    const int player_weight = card_game::get_weight(active_cards);
    const int dealer_weight = card_game::get_weight(dealer_cards);
    // An ace and ten value card after a split are counted as a non-blackjack 21
    const bool player_has_a_blackjack = (active_cards.size() == 2 && player_weight == 21 && !has_split);
    const bool dealer_has_a_blackjack = (dealer_cards.size() == 2 && dealer_weight == 21);
    // If both the dealer and player bust, the player loses
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

cards_t blackjack::open_dealer_cards(state_table::const_iterator state_itr, const checksum256& rand, labels_t& deck) {
    cards_t dealer_cards{state_itr->dealer_card};
    // dealer should stand on soft 17
    for (int i = 0; card_game::get_weight(dealer_cards) <= 16; i++) {
        check(!deck.empty(), "empty deck while opening dealer's cards");
        dealer_cards.push_back(card(*deck.begin()));
        deck.erase(deck.begin());
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
    // dealer wins
    return -ante;
}

asset get_pair_win(const cards_t& cards, asset qty) {
    check(cards.size() == 2, "invalid cards size");
    if (cards[0].get_value() == cards[1].get_value()) {
        return 25 * qty;
    } else if (cards[0].get_rank() == cards[1].get_rank()) {
        return 8 * qty;
    }
    return -qty;
}

asset get_first_three_win(cards_t player_cards, card third_card, asset qty) {
    player_cards.push_back(third_card);
    switch(get_combination(player_cards)) {
        case combination::FLUSH:
            return 5 * qty;
        case combination::STRAIGHT:
            return 10 * qty;
        case combination::THREE_OF_A_KIND:
            return 30 * qty;
        case combination::STRAIGHT_FLUSH:
            return 40 * qty;
        case combination::SUITED_THREE_OF_A_KIND:
            return 100 * qty;
        default:
            return -qty;
    }
}

std::tuple<asset, cards_t> blackjack::compare_and_finish(state_table::const_iterator state_itr, asset ante, const checksum256& rand, labels_t&& deck) {
    // returns players win & dealer's cards
    auto dealer_cards = open_dealer_cards(state_itr, rand, deck);
    auto has_split = state_itr->has_split();
    auto [res, bjack] = compare_cards(state_itr->active_cards, dealer_cards, has_split);
    auto player_win = get_win(ante, res, bjack);
    eosio::print_f("player's 1st round win: %s\n", player_win.to_string());
    if (has_split) {
        std::tie(res, bjack) = compare_cards(state_itr->split_cards, dealer_cards, true);
        const auto split_win = get_win(state_itr->first_round_ante, res, bjack);
        player_win += split_win;
        eosio::print_f("player's 2nd round win: %s\n", split_win.to_string());
    }
    // the first card isn't new. it's been dealt at the begining
    dealer_cards.erase(dealer_cards.begin());
    // side bets
    player_win += state_itr->pair_win + state_itr->first_three_win;
    return std::make_tuple(player_win, std::move(dealer_cards));
}

inline void blackjack::check_deposit(asset deposit, asset current_ante, asset prev_round_ante, asset side_bets) {
    eosio::print_f("deposit: %s, current ante: %s, prev round ante: %s\n", deposit, current_ante, prev_round_ante);
    check(deposit == current_ante + prev_round_ante + side_bets, "invalid deposit");
}

void blackjack::on_new_game(uint64_t ses_id) {
    check_params(ses_id);
    require_action(action::bet);
    state.emplace(get_self(), [&](auto& row) {
        row.ses_id = ses_id;
        row.state = game_state::require_bet;
        row.first_round_ante = zero_asset;
        row.pair_win = zero_asset;
        row.first_three_win = zero_asset;
    });
}

void blackjack::on_action(uint64_t ses_id, uint16_t type, std::vector<game_sdk::param_t> params) {
    const auto state_itr = state.require_find(ses_id, "invalid ses_id");
    if (type == action::bet) {
        check(state_itr->state == game_state::require_bet, "game state should be require_bet");
        check(params.size() == 3, "invalid param size");
        check_bet(ses_id, params[0], params[1], params[2]);
        const auto itr = bet.emplace(get_self(), [&](auto& row) {
            row.ses_id = ses_id;
            row.ante = asset(params[0], core_symbol);
            row.pair = asset(params[1], core_symbol);
            row.first_three = asset(params[2], core_symbol);
        });
        update_max_win(5 * itr->ante + 25 * itr->pair + 100 * itr->first_three);
        update_state(state_itr, game_state::deal_cards);
    } else if (type == action::play) {
        check(state_itr->state == game_state::require_play, "game state should be require_play");
        check(params.size() == 1, "invalid param size");
        const auto bet_itr = bet.require_find(ses_id, "invalid ses_id");
        const auto ante = bet_itr->ante;
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
                check(state_itr->active_cards.size() == 2, "cannot split");
                check(card_game::get_weight(state_itr->active_cards[0]) ==
                      card_game::get_weight(state_itr->active_cards[1]), "cannot split cards with different weights");
                check_deposit(get_session(ses_id).deposit, ante * 2, zero_asset, bet_itr->side_bets_sum());
                // split cards
                state.modify(state_itr, get_self(), [&](auto& row) {
                    row.split_cards.push_back(row.active_cards.back());
                    row.active_cards.pop_back();
                    row.first_round_ante = ante;
                });
                update_state(state_itr, game_state::split);
                break;
            case decision::double_down: {
                check(!state_itr->has_hit(), "player's already hit");
                check(!state_itr->active_cards.empty(), "cards have not been dealt yet");
                const auto& cards = state_itr->active_cards;
                // https://wizardofodds.com/games/blackjack/strategy/european/
                const auto w = card_game::get_weight(cards);
                const auto hard = card_game::is_hard(cards);
                check(9 <= w && w <= 11 && hard, "player may only double on hard totals of 9-11");
                check_deposit(get_session(ses_id).deposit, ante * 2, state_itr->first_round_ante, bet_itr->side_bets_sum());
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
    const auto bet_itr = bet.require_find(ses_id, "invalid ses_id");
    const auto ante = bet_itr->ante;

    switch (state_itr->state) {
        case game_state::deal_cards: {
            eosio::print("dealing cards");
            auto [res, player_cards, dealer_cards] = deal_initial_cards(state_itr, rand);
            asset side_bets_win;
            state.modify(state_itr, get_self(), [&, player_cards = player_cards, dealer_card = dealer_cards[0]](auto& row) {
                row.pair_win = get_pair_win(player_cards, bet_itr->pair);
                row.first_three_win = get_first_three_win(player_cards, dealer_card, bet_itr->first_three);
                side_bets_win = row.pair_win + row.first_three_win;
            });

            if (res == outcome::draw) {
                // both players have a blackjack
                eosio::print("both dealer and player get a blackjack");
                end_game(get_session(ses_id).deposit + side_bets_win, std::move(dealer_cards), std::move(player_cards));
                return;
            } else if (res == outcome::player) {
                // player has a blackjack. It pays 3:2
                eosio::print_f("player gets a blackjack, player: {%, %}, dealer: {%, %}\n",
                                player_cards[0].to_string(), player_cards[1].to_string(),
                                dealer_cards[0].to_string(), dealer_cards[1].to_string());
                end_game(get_session(ses_id).deposit + 3 * ante / 2 + side_bets_win, std::move(dealer_cards), std::move(player_cards));
                return;
            }
            update_state(state_itr, game_state::require_play);
            require_action(action::play);
            send_game_message(std::vector<param_t>{player_cards[0].get_value(), player_cards[1].get_value(), dealer_cards[0].get_value()});
            break;
        }
        case game_state::deal_one_card: {
            eosio::print("player hits");
            auto [res, player_card, deck] = deal_a_card(state_itr, rand);
            if (res == outcome::dealer || card_game::get_weight(state_itr->active_cards) == 21) {
                if (!state_itr->has_split() || state_itr->second_round) {
                    auto [win, dealer_cards] = compare_and_finish(state_itr, ante, rand, std::move(deck));
                    end_game(get_session(ses_id).deposit + win, std::move(dealer_cards), {player_card});
                    return;
                } else {
                    finish_first_round(state_itr);
                }
            }
            send_game_message(std::vector<param_t>{player_card.get_value()});
            update_state(state_itr, game_state::require_play);
            require_action(action::play);
            break;
        }
        case game_state::double_down: {
            eosio::print("player doubles down");
            auto [res, player_card, deck] = deal_a_card(state_itr, rand);
            check(res == outcome::carry_on, "invariant check failed: player cannot bust when doubling");
            if (!state_itr->has_split() || state_itr->second_round) {
                auto [win, dealer_cards] = compare_and_finish(state_itr, ante * 2, rand, std::move(deck));
                end_game(get_session(ses_id).deposit + win, std::move(dealer_cards), {player_card});
                return;
            } else {
                state.modify(state_itr, get_self(), [&](auto& row) {
                    row.first_round_ante *= 2;
                });
                finish_first_round(state_itr);
            }
            send_game_message(std::vector<param_t>{player_card.get_value()});
            update_state(state_itr, game_state::require_play);
            require_action(action::play);
            break;
        }
        case game_state::stand: {
            auto [win, dealer_cards] = compare_and_finish(state_itr, ante, rand, prepare_deck(state_itr, rand));
            end_game(get_session(ses_id).deposit + win, std::move(dealer_cards));
            break;
        }
        case game_state::split: {
            eosio::print("player splits");
            // take 2 cards from the deck and send them to frontend
            auto deck = prepare_deck(state_itr, rand);
            const auto ncard1 = card(deck[0]), ncard2 = card(deck[1]);
            const bool aces = state_itr->active_cards[0].get_rank() == card_game::rank::ACE;
            deck.erase(deck.begin(), deck.begin() + 2);
            state.modify(state_itr, get_self(), [&](auto& row) {
                row.active_cards.push_back(ncard1);
                row.split_cards.push_back(ncard2);
            });
            if (!aces) {
                if (card_game::get_weight(state_itr->active_cards) == 21) {
                    finish_first_round(state_itr);
                    if (card_game::get_weight(state_itr->active_cards) == 21) {
                        auto [win, dealer_cards] = compare_and_finish(state_itr, ante, rand, std::move(deck));
                        end_game(get_session(ses_id).deposit + win, std::move(dealer_cards), {ncard1, ncard2});
                        return;
                    }
                }
                send_game_message(std::vector<param_t>{
                    ncard1.get_value(),
                    ncard2.get_value()
                });
                update_state(state_itr, game_state::require_play);
                require_action(action::play);
            } else {
                // In most casinos the player is only allowed to draw one card on each split ace
                // As a general rule, a ten on a split ace (or vice versa) is not considered a natural blackjack and does not get any bonus
                auto [win, dealer_cards] = compare_and_finish(state_itr, ante, rand, std::move(deck));
                end_game(get_session(ses_id).deposit + win, std::move(dealer_cards));
            }
            break;
        }
        default:
            check(0, "invalid game state");
    }
}

void blackjack::on_finish(uint64_t ses_id) {
    const auto state_itr = state.find(ses_id);
    if (state_itr != state.end()) {
        state.erase(state_itr);
    }
    const auto bet_itr = bet.find(ses_id);
    if (bet_itr != bet.end()) {
        bet.erase(bet_itr);
    }
}

#ifndef IS_DEBUG
GAME_CONTRACT(blackjack)
#else
GAME_CONTRACT_CUSTOM_ACTIONS(blackjack, (pushlabels))
#endif
} // namespace blackjack
