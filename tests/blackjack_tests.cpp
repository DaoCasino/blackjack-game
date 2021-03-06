#define TEST 1

#include <iostream>

#include <game_tester/game_tester.hpp>
#include <game_tester/strategy.hpp>
#include <fc/reflect/reflect.hpp>

#include "contracts.hpp"
#include <blackjack/card.hpp>

namespace testing {

using card_game::card;
using card_game::cards_t;
using card_game::get_weight;

class blackjack_tester : public game_tester {
public:
    static const name game_name;
    static const name player_name;
    static const asset starting_balance;
    static const asset zero_asset;
    static constexpr uint64_t default_ante_min_bet = 1'0000; // 1 BET
    static constexpr uint64_t default_ante_max_bet = 10000'0000; // 10k
    static constexpr uint64_t default_pair_max_bet = 3000'0000; // 3k
    static constexpr uint64_t default_first_three_max_bet = 1000'0000; // 1k
    static constexpr uint64_t default_max_payout = 100000'0000; // 100k BET
public:
    blackjack_tester() {
        create_account(game_name);

        game_params_type game_params = {
            {0, default_ante_min_bet},
            {1, default_ante_max_bet},
            {2, default_max_payout},
            {3, default_pair_max_bet},
            {4, default_first_three_max_bet}
        };
        deploy_game<blackjack_game>(game_name, game_params);
        create_player(player_name);
        link_game(player_name, game_name);
        transfer(N(eosio), player_name, starting_balance);
        transfer(N(eosio), casino_name, starting_balance);
    }

    void bet(uint64_t ses_id, asset ante, asset pair = zero_asset, asset first_three = zero_asset) {
        game_action(game_name, ses_id, 0, {
            static_cast<uint64_t>(ante.get_amount()),
            static_cast<uint64_t>(pair.get_amount()),
            static_cast<uint64_t>(first_three.get_amount())
        });
    }

    void hit(uint64_t ses_id) {
        game_action(game_name, ses_id, 1, {0});
    }

    void stand(uint64_t ses_id) {
        game_action(game_name, ses_id, 1, {1});
    }

    void split(uint64_t ses_id) {
        game_action(game_name, ses_id, 1, {2}, get_ante(ses_id));
    }

    void double_down(uint64_t ses_id) {
        game_action(game_name, ses_id, 1, {3}, get_ante(ses_id));
    }

    asset get_deposit(uint64_t ses_id) {
        return get_game_session(game_name, ses_id)["deposit"].as<asset>();
    }

    asset get_ante(uint64_t ses_id) {
        return get_bet(ses_id)["ante"].as<asset>();
    }

    fc::variant get_bet(uint64_t ses_id) {
        vector<char> data = get_row_by_account(game_name, game_name, N(bet), ses_id);
        return data.empty() ? fc::variant()
                            : abi_ser[game_name].binary_to_variant("bet_row", data, abi_serializer_max_time);
    }

    fc::variant get_state(uint64_t ses_id) {
        vector<char> data = get_row_by_account(game_name, game_name, N(state), ses_id);
        return data.empty() ? fc::variant()
                            : abi_ser[game_name].binary_to_variant("state_row", data, abi_serializer_max_time);
    }

    void push_cards(uint64_t ses_id, const cards_t& cards) {
        card_game::labels_t labels(cards.size());
        for (int i = 0; i < cards.size(); i++) {
            labels[i] = cards[i].to_string();
        }
        BOOST_REQUIRE_EQUAL(
            push_action(
                game_name,
                N(pushlabels),
                {game_name, N(active)},
                mvo()
                    ("ses_id", ses_id)
                    ("labels", labels)
            ),
            success()
        );
    }

    card_game::cards_t get_game_message_cards() {
        cards_t result;
        if (const std::optional<std::vector<fc::variant>> msg_events = get_events(events_id::game_message); msg_events != std::nullopt){
            const auto& event = msg_events->back();
            const auto values = fc::raw::unpack<std::vector<uint64_t>>(event["msg"].as<bytes>());
            result.resize(values.size());
            for (int i = 0; i < values.size(); i++) {
                result[i] = card(values[i]);
            }
        }
        return result;
    }

    std::vector<param_t> get_game_finish_message() {
        if (const std::optional<std::vector<fc::variant>> msg_events = get_events(events_id::game_finished); msg_events != std::nullopt){
            const auto& event = msg_events->back();
            return fc::raw::unpack<std::vector<uint64_t>>(event["msg"].as<bytes>());
        }
        return {};
    }

    std::pair<cards_t, cards_t> decode_game_finish_message(const std::vector<param_t>& msg) {
        if (msg.empty()) {
            return {{}, {}};
        }
        BOOST_CHECK(msg.size() > msg[0] && msg.size() == 2 + msg[0] + msg[msg[0] + 1]);
        cards_t player_cards, dealer_cards;
        player_cards.reserve(msg[0]);
        int i = 1;
        while (i <= msg[0]) {
            player_cards.push_back(card(msg[i++]));
        }
        dealer_cards.reserve(msg[i++]);
        while (i < msg.size()) {
            dealer_cards.push_back(card(msg[i++]));
        }
        return {player_cards, dealer_cards};
    }

    cards_t get_player_finish_cards() {
        return decode_game_finish_message(get_game_finish_message()).first;
    }

    cards_t get_dealer_finish_cards() {
        return decode_game_finish_message(get_game_finish_message()).second;
    }

    void check_player_win(asset win) {
        BOOST_REQUIRE_EQUAL(get_balance(player_name) - starting_balance, win);
        BOOST_REQUIRE_EQUAL(get_balance(casino_name) - starting_balance, -win);
    }
};

const name blackjack_tester::game_name = N(blackjack);
const name blackjack_tester::player_name = N(player);
const asset blackjack_tester::starting_balance = STRSYM("80000000.0000");
const asset blackjack_tester::zero_asset = STRSYM("0.0000");

BOOST_AUTO_TEST_SUITE(blackjack_tests)

BOOST_FIXTURE_TEST_CASE(new_game_test_min_deposit_fail, blackjack_tester) try {
    const auto ses_id = 0u;
    transfer(player_name, game_name, STRSYM("0.5000"), std::to_string(ses_id));
    BOOST_REQUIRE_EQUAL(
        push_action(
            game_name,
            N(newgame),
            {platform_name, N(gameaction)},
            mvo()("req_id", ses_id)("casino_id", casino_id)
        ),
        wasm_assert_msg("deposit is less than min bet")
    );
} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE(new_game_test_min_deposit_ok, blackjack_tester) try {
    const auto ses_id = 0u;
    transfer(player_name, game_name, STRSYM("1.0000"), std::to_string(ses_id));
    BOOST_REQUIRE_EQUAL(
        push_action(
            game_name,
            N(newgame),
            {platform_name, N(gameaction)},
            mvo()("req_id", ses_id)("casino_id", casino_id)
        ),
        success()
    );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(new_game_test_max_deposit_fail, blackjack_tester) try {
    const auto ses_id = 0u;
    transfer(player_name, game_name, STRSYM("300000.0000"), std::to_string(ses_id));
    BOOST_REQUIRE_EQUAL(
        push_action(
            game_name,
            N(newgame),
            {platform_name, N(gameaction)},
            mvo()("req_id", ses_id)("casino_id", casino_id)
        ),
        wasm_assert_msg("deposit exceeds max payout")
    );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(new_game_test_max_deposit_ok, blackjack_tester) try {
    const auto ses_id = 0u;
    transfer(player_name, game_name, STRSYM("100000.0000"), std::to_string(ses_id));
    BOOST_REQUIRE_EQUAL(
        push_action(
            game_name,
            N(newgame),
            {platform_name, N(gameaction)},
            mvo()("req_id", ses_id)("casino_id", casino_id)
        ),
        success()
    );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(bet_action, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("100.0000"));
    BOOST_REQUIRE_EQUAL(
        push_action(
            game_name,
            N(gameaction),
            {platform_name, N(gameaction)},
            mvo()("req_id", ses_id)("type", 0)("params", std::vector<param_t>{100'0000, 0, 0})
        ),
        success()
    );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(invalid_action, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("100.0000"));
    BOOST_REQUIRE_EQUAL(
        push_action(
            game_name,
            N(gameaction),
            {platform_name, N(gameaction)},
            mvo()("req_id", ses_id)("type", 2)("params", std::vector<param_t>{0})
        ),
        wasm_assert_msg("invalid action")
    );
} FC_LOG_AND_RETHROW()

#ifdef IS_DEBUG

char hard_decision[10][10] = {
    {'H', 'H', 'H', 'H', 'H', 'H', 'H', 'H', 'H', 'H'},
    {'H', 'D', 'D', 'D', 'D', 'H', 'H', 'H', 'H', 'H'},
    {'D', 'D', 'D', 'D', 'D', 'D', 'D', 'D', 'H', 'H'},
    {'D', 'D', 'D', 'D', 'D', 'D', 'D', 'D', 'H', 'H'},
    {'H', 'H', 'S', 'S', 'S', 'H', 'H', 'H', 'H', 'H'},
    {'S', 'S', 'S', 'S', 'S', 'H', 'H', 'H', 'H', 'H'},
    {'S', 'S', 'S', 'S', 'S', 'H', 'H', 'H', 'H', 'H'},
    {'S', 'S', 'S', 'S', 'S', 'H', 'H', 'H', 'H', 'H'},
    {'S', 'S', 'S', 'S', 'S', 'H', 'H', 'H', 'H', 'H'},
    {'S', 'S', 'S', 'S', 'S', 'S', 'S', 'S', 'S', 'S'}
};

char soft_decision[3][10] = {
    {'H', 'H', 'H', 'H', 'H', 'H', 'H', 'H', 'H', 'H'},
    {'S', 'S', 'S', 'S', 'S', 'S', 'S', 'H', 'H', 'H'},
    {'S', 'S', 'S', 'S', 'S', 'S', 'S', 'S', 'S', 'S'}
};

char pair_decision[10][10] = {
    {'P', 'P', 'P', 'P', 'P', 'P', 'H', 'H', 'H', 'H'},
    {'P', 'P', 'P', 'P', 'P', 'P', 'H', 'H', 'H', 'H'},
    {'H', 'H', 'H', 'P', 'P', 'H', 'H', 'H', 'H', 'H'},
    {'D', 'D', 'D', 'D', 'D', 'D', 'D', 'D', 'H', 'H'},
    {'P', 'P', 'P', 'P', 'P', 'H', 'H', 'H', 'H', 'H'},
    {'P', 'P', 'P', 'P', 'P', 'P', 'H', 'H', 'H', 'H'},
    {'P', 'P', 'P', 'P', 'P', 'P', 'P', 'P', 'H', 'H'},
    {'P', 'P', 'P', 'P', 'P', 'S', 'P', 'P', 'S', 'S'},
    {'S', 'S', 'S', 'S', 'S', 'S', 'S', 'S', 'S', 'S'},
    {'P', 'P', 'P', 'P', 'P', 'P', 'P', 'P', 'P', 'H'}
};

char get_decision(int player_sum, int dealer_rank, bool hard, bool pair, bool aces) {
    char d;
    if (pair) {
        if (aces) {
            // pair of aces (1 + 11)
            d = pair_decision[9][dealer_rank];
        } else {
            d = pair_decision[player_sum / 2 - 2][dealer_rank];
        }
    } else if (hard) {
        BOOST_CHECK(5 <= player_sum);
        if (player_sum <= 8) {
            d = hard_decision[0][dealer_rank];
        } else if (player_sum <= 16) {
            d = hard_decision[player_sum - 8][dealer_rank];
        } else {
            d = hard_decision[9][dealer_rank];
        }
    } else {
        // A (11) + 2
        BOOST_CHECK(13 <= player_sum);
        if (player_sum <= 17) {
            d = soft_decision[0][dealer_rank];
        } else if (player_sum == 18) {
            d = soft_decision[1][dealer_rank];
        } else {
            d = soft_decision[2][dealer_rank];
        }
    }
    return d;
}

const int ROUNDS_PER_BATCH = 1000;

std::pair<asset, asset> get_batch_result() {
    blackjack_tester t;
    const asset before_batch_balance = t.get_balance(blackjack_tester::player_name);
    const asset bet_amount = STRSYM("1.0000");
    const asset deposit_amount = STRSYM("1.0000");
    asset all_bets_sum = STRSYM("0.0000");

    for (int i = 0; i < ROUNDS_PER_BATCH; i++) {
        const auto before_round_balance = t.get_balance(blackjack_tester::player_name);
        const auto ses_id = t.new_game_session(blackjack_tester::game_name, blackjack_tester::player_name, blackjack_tester::casino_id, deposit_amount);
        all_bets_sum += bet_amount;
        t.bet(ses_id, bet_amount);
        t.signidice(blackjack_tester::game_name, ses_id);
        const auto initial_cards = t.get_game_message_cards();

        if (!initial_cards.empty()) {
            // no blackjack at the begining
            BOOST_TEST_MESSAGE("Initial cards dealt: " << initial_cards);
            const auto dealer_card = initial_cards.back();
            const auto dealer_shifted_rank = (dealer_card.get_rank() == card_game::rank::ACE ? 11 : card_game::get_weight(dealer_card)) - 2;
            fc::variant state;
            bool game_finished = false;
            while (!game_finished) {
                const auto& state = t.get_state(ses_id);
                const bool has_split = !state["split_cards"].as<cards_t>().empty();
                auto cards = state["active_cards"].as<cards_t>();
                BOOST_TEST_MESSAGE("Player's cards: " << cards);
                const bool pair = (cards.size() == 2 && cards[0].get_rank() == cards[1].get_rank());
                const char d = get_decision(card_game::get_weight(cards), dealer_shifted_rank, card_game::is_hard(cards), pair, cards[0].get_rank() == card_game::rank::ACE);
                BOOST_TEST_MESSAGE("Decision: " << d << " sum: " << card_game::get_weight(cards));
                switch(d) {
                case 'H':
                    t.hit(ses_id);
                    break;
                case 'S':
                    t.stand(ses_id);
                    break;
                case 'D':
                    if (cards.size() == 2) {
                        t.double_down(ses_id);
                        all_bets_sum += bet_amount;
                    } else {
                        // otherwise just hit
                        t.hit(ses_id);
                    }
                    break;
                case 'P':
                    if (!has_split) {
                        t.split(ses_id);
                        all_bets_sum += bet_amount;
                    } else {
                        t.hit(ses_id);
                    }
                    break;
                default:
                    throw std::logic_error("unknown decision");
                }

                if (d == 'S' && has_split && !state["second_round"].as<bool>()) {
                    continue;
                }
                t.signidice(t.game_name, ses_id);
                auto dealer_cards = t.get_dealer_finish_cards();
                if (!dealer_cards.empty()) {
                    game_finished = true;
                    dealer_cards.insert(dealer_cards.begin(), dealer_card);
                    BOOST_TEST_MESSAGE("Dealer opens with " << dealer_cards);
                }
            }
        } else {
            const cards_t pcards = t.get_player_finish_cards(), dcards = t.get_dealer_finish_cards();
            BOOST_TEST_MESSAGE("Player gets a blackjack, player: " << pcards << " dealer: " << dcards);
        }
        BOOST_TEST_MESSAGE("Player's win: " << t.get_balance(t.player_name) - before_round_balance);
        BOOST_TEST_MESSAGE("================");
    }
    return std::make_pair(t.get_balance(t.player_name) - before_batch_balance, all_bets_sum);
}

std::pair<asset, asset> get_side_bet_batch_result(asset pair, asset first_three) {
    blackjack_tester t;
    const asset before_batch_balance = t.get_balance(blackjack_tester::player_name);
    asset ante = STRSYM("1.0000");
    const asset deposit = ante + pair + first_three;
    asset all_side_bets_sum = blackjack_tester::zero_asset;
    asset ante_win_sum = STRSYM("0.0000");
    for (int i = 0; i < ROUNDS_PER_BATCH; i++) {
        const auto before_round_balance = t.get_balance(blackjack_tester::player_name);
        const auto ses_id = t.new_game_session(blackjack_tester::game_name, blackjack_tester::player_name, blackjack_tester::casino_id, deposit);
        all_side_bets_sum += deposit - ante;
        t.bet(ses_id, ante, pair, first_three);
        t.signidice(t.game_name, ses_id);
        const auto initial_cards = t.get_game_message_cards();
        const bool blackjack = initial_cards.empty();
        if (!blackjack) {
            t.stand(ses_id);
            t.signidice(t.game_name, ses_id);
        }
        cards_t player_cards, dealer_cards;
        if (!blackjack) {
            player_cards = cards_t{initial_cards[0], initial_cards[1]};
            dealer_cards = t.get_dealer_finish_cards();
            // add dealer's open card
            dealer_cards.push_back(initial_cards[2]);
        } else {
            player_cards = t.get_player_finish_cards();
            dealer_cards = t.get_dealer_finish_cards();
        }
        const auto player_weight = card_game::get_weight(player_cards), dealer_weight = card_game::get_weight(dealer_cards);
        asset maingame_win = t.zero_asset;
        if (dealer_weight > 21 || player_weight > dealer_weight) {
            maingame_win += blackjack ? asset(ante.get_amount() * 3 / 2, symbol(CORE_SYM)) : ante;
        } else if (player_weight < dealer_weight) {
            maingame_win -= ante;
        }
        ante_win_sum += maingame_win;
        BOOST_TEST_MESSAGE("Player cards: " << player_cards << " dealer: " << dealer_cards << " side bet win: "
                            << t.get_balance(t.player_name) - before_round_balance - maingame_win);
    }
    return std::make_pair(t.get_balance(t.player_name) - before_batch_balance - ante_win_sum, all_side_bets_sum);
}

inline double to_double(const asset& value) {
    return double(value.get_amount()) / value.precision();
}

typedef std::function<std::pair<asset, asset>()> batch_runner_t;

double get_rtp(batch_runner_t&& batch_runner_fn) {
    const int rounds = 1'000'000;
    const int batches = rounds / ROUNDS_PER_BATCH;
    asset returned = STRSYM("0.0000");
    asset all_bets_sum = STRSYM("0.0000");
    for (int i = 0; i < batches; i++) {
        const auto [r, b] = batch_runner_fn();
        returned += r;
        all_bets_sum += b;
        std::cerr << "Batch #" << i + 1 << " completed, rtp: " << to_double(returned) / to_double(all_bets_sum) + 1 << "\n";
    }
    return to_double(returned) / to_double(all_bets_sum) + 1;
}

BOOST_AUTO_TEST_CASE(rtp_maingame_test, *boost::unit_test::disabled()) try {
    BOOST_TEST(get_rtp(get_batch_result) == 0.993, boost::test_tools::tolerance(0.001));
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE(rtp_pair_test, *boost::unit_test::disabled()) try {
    auto lambda = [](){ return get_side_bet_batch_result(STRSYM("1.0000"), STRSYM("0.0000")); };
    BOOST_TEST(get_rtp(lambda) == 0.96, boost::test_tools::tolerance(0.05));
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE(rtp_first_three_test, *boost::unit_test::disabled()) try {
    auto lambda = [](){ return get_side_bet_batch_result(STRSYM("0.0000"), STRSYM("1.0000")); };
    BOOST_TEST(get_rtp(lambda) == 0.963, boost::test_tools::tolerance(0.05));
} FC_LOG_AND_RETHROW()

// ----------------------------

BOOST_FIXTURE_TEST_CASE(invalid_decision, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("100.0000"));
    bet(ses_id, STRSYM("100.0000"));
    push_cards(ses_id, {"3d", "Ts", "2c", "7c"});
    signidice(game_name, ses_id);
    BOOST_REQUIRE_EQUAL(
        push_action(
            game_name,
            N(gameaction),
            {platform_name, N(gameaction)},
            mvo()("req_id", ses_id)("type", 1)("params", std::vector<param_t>{5})
        ),
        wasm_assert_msg("invalid decision")
    );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(player_has_a_blackjack, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("100.0000"));
    bet(ses_id, STRSYM("100.0000"));

    push_cards(ses_id, {"Ad", "Ts", "2c", "7c"});
    signidice(game_name, ses_id);

    check_player_win(STRSYM("150.0000"));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(player_dealer_both_have_a_blackjack, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("100.0000"));
    bet(ses_id, STRSYM("100.0000"));

    push_cards(ses_id, {"Ad", "Ts", "Td", "As"});
    signidice(game_name, ses_id);

    check_player_win(STRSYM("0.0000"));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(dealer_has_a_blackjack, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("100.0000"));
    bet(ses_id, STRSYM("100.0000"));

    // Tc is a hole card
    push_cards(ses_id, {"Kd", "Ts", "Td"});
    signidice(game_name, ses_id);

    stand(ses_id);
    push_cards(ses_id, {"As"});
    signidice(game_name, ses_id);
    // dealer has Td As
    check_player_win(-STRSYM("100.0000"));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(player_hits_and_busts, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("100.0000"));
    bet(ses_id, STRSYM("100.0000"));

    push_cards(ses_id, {"Kd", "Ts", "Td"});
    signidice(game_name, ses_id);

    hit(ses_id);
    push_cards(ses_id, {"3d", "Jh"});
    signidice(game_name, ses_id);

    check_player_win(-STRSYM("100.0000"));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(player_hits_and_wins, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("100.0000"));
    bet(ses_id, STRSYM("100.0000"));

    push_cards(ses_id, {"Kd", "Ts", "Td"});
    signidice(game_name, ses_id);

    // player gets a 21 not a blackjack though
    hit(ses_id);
    push_cards(ses_id, {"Ac", "Qs"});
    signidice(game_name, ses_id);

    check_player_win(STRSYM("100.0000"));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(player_hits_two_times_and_wins, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("100.0000"));
    bet(ses_id, STRSYM("100.0000"));

    // intial sum = 15
    push_cards(ses_id, {"Kd", "5s", "Td"});
    signidice(game_name, ses_id);

    // first hit
    hit(ses_id);
    push_cards(ses_id, {"4s"});
    signidice(game_name, ses_id);

    // second hit
    hit(ses_id);
    push_cards(ses_id, {"Ad"});
    signidice(game_name, ses_id);

    // dealer's turn
    stand(ses_id);
    push_cards(ses_id, {"5c", "4d"});
    signidice(game_name, ses_id);

    check_player_win(STRSYM("100.0000"));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(player_doubles_and_wins, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("100.0000"));
    bet(ses_id, STRSYM("100.0000"));

    // intial sum = 11
    push_cards(ses_id, {"6d", "5s", "Td"});
    signidice(game_name, ses_id);

    double_down(ses_id);
    push_cards(ses_id, {"8s", "7d"});
    signidice(game_name, ses_id);

    check_player_win(STRSYM("200.0000"));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(player_doubles_and_loses, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("100.0000"));
    bet(ses_id, STRSYM("100.0000"));

    // intial sum = 11
    push_cards(ses_id, {"6d", "5s", "Td"});
    signidice(game_name, ses_id);

    double_down(ses_id);
    push_cards(ses_id, {"8s", "Kh"});
    signidice(game_name, ses_id);

    check_player_win(-STRSYM("200.0000"));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(check_state_after_split, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("100.0000"));
    bet(ses_id, STRSYM("100.0000"));

    push_cards(ses_id, {"6d", "6s", "Td"});
    signidice(game_name, ses_id);

    split(ses_id);
    push_cards(ses_id, {"8s", "Kh"});
    signidice(game_name, ses_id);

    const auto& state = get_state(ses_id);
    const cards_t active_cards{"6d", "8s"}, split_cards{"6s", "Kh"};
    BOOST_REQUIRE_EQUAL(state["active_cards"].as<cards_t>(), active_cards);
    BOOST_REQUIRE_EQUAL(state["split_cards"].as<cards_t>(), split_cards);
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(player_split_win_win, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("100.0000"));
    bet(ses_id, STRSYM("100.0000"));

    push_cards(ses_id, {"6d", "6s", "Td"});
    signidice(game_name, ses_id);

    split(ses_id);
    push_cards(ses_id, {"8s", "Kh"});
    signidice(game_name, ses_id);

    // player hits with 6d 8s and gets 5h, total = 19
    hit(ses_id);
    push_cards(ses_id, {"5h"});
    signidice(game_name, ses_id);
    stand(ses_id);

    // now player hits with 6s Kh and gets 4c, total = 20
    hit(ses_id);
    push_cards(ses_id, {"4c"});
    signidice(game_name, ses_id);

    // open dealer's cards, Td 2c 6h, total = 18
    stand(ses_id);
    push_cards(ses_id, {"2c", "6h"});
    signidice(game_name, ses_id);

    check_player_win(STRSYM("200.0000"));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(player_split_win_lose, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("100.0000"));
    bet(ses_id, STRSYM("100.0000"));

    push_cards(ses_id, {"6d", "6s", "Td"});
    signidice(game_name, ses_id);

    split(ses_id);
    push_cards(ses_id, {"8s", "Kh"});
    signidice(game_name, ses_id);

    // player hits with 6d 8s and gets 5h, total = 18
    hit(ses_id);
    push_cards(ses_id, {"4h"});
    signidice(game_name, ses_id);
    stand(ses_id);

    // now player hits with 6s Kh and gets 4c, total = 20
    hit(ses_id);
    push_cards(ses_id, {"4c"});
    signidice(game_name, ses_id);

    // open dealer's cards, Td 2c 6h, total = 19
    stand(ses_id);
    push_cards(ses_id, {"9h"});
    signidice(game_name, ses_id);

    check_player_win(STRSYM("0.0000"));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(player_split_doubles_lose_win, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("100.0000"));
    bet(ses_id, STRSYM("100.0000"));

    push_cards(ses_id, {"6d", "6s", "Td"});
    signidice(game_name, ses_id);

    split(ses_id);
    push_cards(ses_id, {"5s", "Kh"});
    signidice(game_name, ses_id);

    // player hits with 6d 5s and gets 4h, total = 16
    double_down(ses_id);
    push_cards(ses_id, {"4h"});
    signidice(game_name, ses_id);

    // now player hits with 6s Kh and gets 4c, total = 20
    hit(ses_id);
    push_cards(ses_id, {"4c"});
    signidice(game_name, ses_id);

    // open dealer's cards, Td 9h, total = 19
    stand(ses_id);
    push_cards(ses_id, {"9h"});
    signidice(game_name, ses_id);

    check_player_win(-STRSYM("100.0000"));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(player_split_doubles_down_both_loss, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("100.0000"));
    bet(ses_id, STRSYM("100.0000"));

    push_cards(ses_id, {"6d", "6s", "Td"});
    signidice(game_name, ses_id);

    split(ses_id);
    push_cards(ses_id, {"5s", "4d"});
    signidice(game_name, ses_id);

    // player hits with 6d 5s and gets 4h, total = 15
    double_down(ses_id);
    push_cards(ses_id, {"4h"});
    signidice(game_name, ses_id);

    // now open player and dealer open cards
    // player with 6s 4d gets 7s, total = 17
    // dealer with Td gets 9h, total = 19
    double_down(ses_id);
    push_cards(ses_id, {"7s", "9h"});
    signidice(game_name, ses_id);
    check_player_win(-STRSYM("400.0000"));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(player_split_max_loss_case, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("100.0000"));
    bet(ses_id, STRSYM("100.0000"));

    push_cards(ses_id, {"6d", "6s", "Td"});
    signidice(game_name, ses_id);

    split(ses_id);
    push_cards(ses_id, {"5s", "4d"});
    signidice(game_name, ses_id);

    // player hits with 6d 5s and gets 4h, total = 15
    double_down(ses_id);
    push_cards(ses_id, {"4h"});
    signidice(game_name, ses_id);

    // now open player and dealer open cards
    // player with 6s 4d gets 7s, total = 17
    // dealer with Td gets Ah, total = blackjack
    double_down(ses_id);
    push_cards(ses_id, {"7s", "Ah"});
    signidice(game_name, ses_id);
    check_player_win(-STRSYM("400.0000"));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(player_split_doubles_max_win_case, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("100.0000"));
    bet(ses_id, STRSYM("100.0000"));

    push_cards(ses_id, {"6d", "6s", "Td"});
    signidice(game_name, ses_id);

    split(ses_id);
    push_cards(ses_id, {"5s", "4d"});
    signidice(game_name, ses_id);

    // player hits with 6d 5s and gets Jh, total = 21
    double_down(ses_id);
    push_cards(ses_id, {"Jh"});
    signidice(game_name, ses_id);

    // now open player and dealer open cards
    // player with 6s 4d gets Qs, total = 17
    // dealer with Td gets 9h, total = 19
    double_down(ses_id);
    push_cards(ses_id, {"Qs", "9h"});
    signidice(game_name, ses_id);
    check_player_win(STRSYM("400.0000"));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(player_split_double_21, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("100.0000"));
    bet(ses_id, STRSYM("100.0000"));

    push_cards(ses_id, {"Jd", "Js", "Td"});
    signidice(game_name, ses_id);

    split(ses_id);
    push_cards(ses_id, {"As", "Ad", "7s"});
    signidice(game_name, ses_id);

    check_player_win(STRSYM("200.0000"));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(split_aces_case_general, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("100.0000"));
    bet(ses_id, STRSYM("100.0000"));

    push_cards(ses_id, {"Ad", "As", "Td"});
    signidice(game_name, ses_id);

    split(ses_id);
    push_cards(ses_id, {"9s", "Ac", "Qd"});
    signidice(game_name, ses_id);
    check_player_win(-STRSYM("100.0000"));
    const cards_t player_cards{"9s", "Ac"};
    const cards_t dealer_cards{"Qd"};
    BOOST_REQUIRE_EQUAL(get_player_finish_cards(), player_cards);
    BOOST_REQUIRE_EQUAL(get_dealer_finish_cards(), dealer_cards);
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(split_aces_case_blackjack, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("100.0000"));
    bet(ses_id, STRSYM("100.0000"));

    push_cards(ses_id, {"Ad", "As", "Td"});
    signidice(game_name, ses_id);

    split(ses_id);
    push_cards(ses_id, {"Js", "Jd", "7s"});
    signidice(game_name, ses_id);
    check_player_win(STRSYM("200.0000"));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(split_jack_queen, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("100.0000"));
    bet(ses_id, STRSYM("100.0000"));

    push_cards(ses_id, {"Jd", "Qs", "Td"});
    signidice(game_name, ses_id);

    split(ses_id);
    push_cards(ses_id, {"2s", "9d"});
    signidice(game_name, ses_id);

    hit(ses_id);
    push_cards(ses_id, {"7d"});
    signidice(game_name, ses_id);
    stand(ses_id);

    // now Qs 9d
    stand(ses_id);
    push_cards(ses_id, {"Kh"});
    signidice(game_name, ses_id);

    check_player_win(-STRSYM("200.0000"));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(initial_cards_game_message, blackjack_tester) {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("100.0000"));
    bet(ses_id, STRSYM("100.0000"));

    cards_t initial_cards{"Jd", "Js", "Td"};
    push_cards(ses_id, initial_cards);
    signidice(game_name, ses_id);
    BOOST_REQUIRE_EQUAL(get_game_message_cards(), initial_cards);
}

BOOST_FIXTURE_TEST_CASE(initial_cards_blackjack_game_message, blackjack_tester) {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("100.0000"));
    bet(ses_id, STRSYM("100.0000"));

    push_cards(ses_id, {"Ad", "Js", "Td", "7c"});
    signidice(game_name, ses_id);
    cards_t player_cards{"Ad", "Js"}, dealer_cards{"Td", "7c"};
    BOOST_REQUIRE_EQUAL(get_player_finish_cards(), player_cards);
    BOOST_REQUIRE_EQUAL(get_dealer_finish_cards(), dealer_cards);
}

BOOST_FIXTURE_TEST_CASE(hit_game_message, blackjack_tester) {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("100.0000"));
    bet(ses_id, STRSYM("100.0000"));

    cards_t initial_cards{"8c", "Js", "Td"};
    push_cards(ses_id, initial_cards);
    signidice(game_name, ses_id);

    hit(ses_id);
    push_cards(ses_id, {"As"});
    signidice(game_name, ses_id);

    BOOST_REQUIRE_EQUAL(get_game_message_cards(), cards_t{"As"});
}

BOOST_FIXTURE_TEST_CASE(double_down_game_message, blackjack_tester) {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("100.0000"));
    bet(ses_id, STRSYM("100.0000"));

    cards_t initial_cards{"8c", "3s", "Td"};
    push_cards(ses_id, initial_cards);
    signidice(game_name, ses_id);

    double_down(ses_id);
    push_cards(ses_id, {"As", "Kh"});
    signidice(game_name, ses_id);

    BOOST_REQUIRE_EQUAL(get_player_finish_cards(), cards_t{"As"});
    BOOST_REQUIRE_EQUAL(get_dealer_finish_cards(), cards_t{"Kh"});
}

BOOST_FIXTURE_TEST_CASE(split_game_message, blackjack_tester) {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("100.0000"));
    bet(ses_id, STRSYM("100.0000"));

    cards_t initial_cards{"3c", "3s", "Td"};
    push_cards(ses_id, initial_cards);
    signidice(game_name, ses_id);

    cards_t mock_cards{"As", "Kh"};
    split(ses_id);
    push_cards(ses_id, mock_cards);
    signidice(game_name, ses_id);

    BOOST_REQUIRE_EQUAL(get_game_message_cards(), mock_cards);
}

BOOST_FIXTURE_TEST_CASE(split_game_message_blackjack_one, blackjack_tester) {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("100.0000"));
    bet(ses_id, STRSYM("100.0000"));

    push_cards(ses_id, {"Tc", "Qd", "Td"});
    signidice(game_name, ses_id);

    split(ses_id);
    const cards_t player_cards{"As", "7d"};
    push_cards(ses_id, player_cards);
    signidice(game_name, ses_id);
    BOOST_REQUIRE_EQUAL(get_game_message_cards(), player_cards);
}

BOOST_FIXTURE_TEST_CASE(split_game_message_blackjack_both, blackjack_tester) {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("100.0000"));
    bet(ses_id, STRSYM("100.0000"));

    push_cards(ses_id, {"Tc", "Qd", "Td"});
    signidice(game_name, ses_id);

    split(ses_id);
    push_cards(ses_id, {"As", "Ah", "3d", "5h"});
    signidice(game_name, ses_id);
    const cards_t player_cards{"As", "Ah"};
    const cards_t dealer_cards{"3d", "5h"};
    BOOST_REQUIRE_EQUAL(get_player_finish_cards(), player_cards);
    BOOST_REQUIRE_EQUAL(get_dealer_finish_cards(), dealer_cards);
}

// side bets

BOOST_FIXTURE_TEST_CASE(pair_unsuited, blackjack_tester) {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("110.0000"));
    bet(ses_id, STRSYM("100.0000"), STRSYM("10.0000"));

    cards_t initial_cards{"3c", "3s", "Td"};
    push_cards(ses_id, initial_cards);
    signidice(game_name, ses_id);

    push_cards(ses_id, {"As"});
    stand(ses_id);
    signidice(game_name, ses_id);
    // 10 * 8 - 100
    check_player_win(-STRSYM("20.0000"));
}

BOOST_FIXTURE_TEST_CASE(pair_suited, blackjack_tester) {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("110.0000"));
    bet(ses_id, STRSYM("100.0000"), STRSYM("10.0000"));

    cards_t initial_cards{"3s", "3s", "Td"};
    push_cards(ses_id, initial_cards);
    signidice(game_name, ses_id);

    push_cards(ses_id, {"As"});
    stand(ses_id);
    signidice(game_name, ses_id);
    // 10 * 25 - 100
    check_player_win(STRSYM("150.0000"));
}

BOOST_FIXTURE_TEST_CASE(no_pair, blackjack_tester) {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("110.0000"));
    bet(ses_id, STRSYM("100.0000"), STRSYM("10.0000"));

    cards_t initial_cards{"Ac", "Kh", "Td", "Jd"};
    push_cards(ses_id, initial_cards);
    signidice(game_name, ses_id);
    // 100 * 1.5 - 10
    check_player_win(STRSYM("140.0000"));
}

BOOST_FIXTURE_TEST_CASE(first_three_bet_loss, blackjack_tester) {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("110.0000"));
    bet(ses_id, STRSYM("100.0000"), STRSYM("0.0000"), STRSYM("10.0000"));

    cards_t initial_cards{"3c", "3h", "4d"};
    push_cards(ses_id, initial_cards);
    signidice(game_name, ses_id);

    stand(ses_id);
    push_cards(ses_id, {"Ad", "5c"});
    signidice(game_name, ses_id);
    check_player_win(STRSYM("-110.0000"));
}

BOOST_FIXTURE_TEST_CASE(first_three_bet_three_of_a_kind, blackjack_tester) {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("110.0000"));
    bet(ses_id, STRSYM("100.0000"), STRSYM("0.0000"), STRSYM("10.0000"));

    cards_t initial_cards{"3c", "3h", "3d"};
    push_cards(ses_id, initial_cards);
    signidice(game_name, ses_id);

    stand(ses_id);
    push_cards(ses_id, {"Ad", "5c"});
    signidice(game_name, ses_id);
    // 10 * 30 - 100
    check_player_win(STRSYM("200.0000"));
}

BOOST_FIXTURE_TEST_CASE(first_three_bet_straight, blackjack_tester) {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("110.0000"));
    bet(ses_id, STRSYM("100.0000"), STRSYM("0.0000"), STRSYM("10.0000"));

    cards_t initial_cards{"Ts", "Jh", "Qs"};
    push_cards(ses_id, initial_cards);
    signidice(game_name, ses_id);

    stand(ses_id);
    push_cards(ses_id, {"Kd"});
    signidice(game_name, ses_id);
    // 10 * 10
    check_player_win(STRSYM("100.0000"));
}

BOOST_FIXTURE_TEST_CASE(first_three_bet_straight_flush, blackjack_tester) {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("110.0000"));
    bet(ses_id, STRSYM("100.0000"), STRSYM("0.0000"), STRSYM("10.0000"));

    cards_t initial_cards{"Ts", "Js", "Qs"};
    push_cards(ses_id, initial_cards);
    signidice(game_name, ses_id);

    stand(ses_id);
    push_cards(ses_id, {"Kd"});
    signidice(game_name, ses_id);
    // 10 * 40
    check_player_win(STRSYM("400.0000"));
}

BOOST_FIXTURE_TEST_CASE(pair_bet_double_down, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("110.0000"));
    bet(ses_id, STRSYM("100.0000"), STRSYM("10.0000"), STRSYM("0.0000"));

    push_cards(ses_id, {"5d", "5s", "Td"});
    signidice(game_name, ses_id);

    double_down(ses_id);
    push_cards(ses_id, {"Jd", "Qc"});
    signidice(game_name, ses_id);

    check_player_win(STRSYM("80.0000"));
} FC_LOG_AND_RETHROW()

// max payout tests

BOOST_FIXTURE_TEST_CASE(max_payout_basic, blackjack_tester) {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("2001.0000"));
    bet(ses_id, STRSYM("1.0000"), STRSYM("1000.0000"), STRSYM("1000.0000"));

    push_cards(ses_id, {"3c", "3c", "3c"});
    signidice(game_name, ses_id);

    stand(ses_id);
    push_cards(ses_id, {"Ad", "7c"});
    signidice(game_name, ses_id);
    // min(100k + 25k - 1, 100k)
    check_player_win(STRSYM("100000.0000"));
}

BOOST_FIXTURE_TEST_CASE(max_payout_double, blackjack_tester) {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("4001.0000"));
    bet(ses_id, STRSYM("1.0000"), STRSYM("3000.0000"), STRSYM("1000.0000"));

    push_cards(ses_id, {"5s", "5s", "5h"});
    signidice(game_name, ses_id);

    double_down(ses_id);
    push_cards(ses_id, {"Qd", "Qc", "Th"});
    signidice(game_name, ses_id);
    // min(75k + 30k, 100k)
    check_player_win(STRSYM("100000.0000"));
}

BOOST_FIXTURE_TEST_CASE(max_payout_split, blackjack_tester) {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("4001.0000"));
    bet(ses_id, STRSYM("1.0000"), STRSYM("3000.0000"), STRSYM("1000.0000"));

    push_cards(ses_id, {"7s", "7s", "7h"});
    signidice(game_name, ses_id);

    split(ses_id);
    push_cards(ses_id, {"9h", "As"});
    signidice(game_name, ses_id);

    stand(ses_id);
    stand(ses_id);
    push_cards(ses_id, {"5d", "Kh", "Qc"});
    signidice(game_name, ses_id);
    // min(75k + 30k + 1 + 1, 100k)
    check_player_win(STRSYM("100000.0000"));
}

#endif

BOOST_AUTO_TEST_SUITE_END()

} // namespace testing
