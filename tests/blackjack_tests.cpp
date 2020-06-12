#define TEST 1

#include <game_tester/game_tester.hpp>
#include <game_tester/strategy.hpp>
#include <fc/reflect/reflect.hpp>

#include "contracts.hpp"
#include <blackjack/card.hpp>

namespace testing {

using card_game::card;
using card_game::cards_t;

class blackjack_tester : public game_tester {
public:
    static const name game_name;
    static const name player_name;
    static const asset starting_balance;
    static constexpr uint64_t default_ante_min_bet = 1'0000; // 1 BET
    static constexpr uint64_t default_ante_max_bet = 10000'0000;
    static constexpr uint64_t default_max_payout = 100000'0000; // 100k BET
public:
    blackjack_tester() {
        create_account(game_name);

        game_params_type game_params = {
            {0, default_ante_min_bet},
            {1, default_ante_max_bet},
            {2, default_max_payout}
        };
        deploy_game<blackjack_game>(game_name, game_params);
        create_player(player_name);
        link_game(player_name, game_name);
        transfer(N(eosio), player_name, starting_balance);
        transfer(N(eosio), casino_name, starting_balance);
    }

    void bet(uint64_t ses_id, asset ante) {
        game_action(game_name, ses_id, 0, {static_cast<uint64_t>(ante.get_amount())});
    }

    void hit(uint64_t ses_id) {
        game_action(game_name, ses_id, 1, {0});
    }

    void stand(uint64_t ses_id) {
        game_action(game_name, ses_id, 1, {1});
    }

    void split(uint64_t ses_id) {
        game_action(game_name, ses_id, 1, {2}, asset(get_ante(ses_id).get_amount() * 3 / 2));
    }

    void double_down(uint64_t ses_id) {
        game_action(game_name, ses_id, 1, {3}, asset(get_ante(ses_id).get_amount() * 3 / 2));
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

    void push_cards(uint64_t ses_id, const card_game::labels_t& labels) {
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

    void check_player_win(asset win) {
        BOOST_REQUIRE_EQUAL(get_balance(player_name), starting_balance + win);
        BOOST_REQUIRE_EQUAL(get_balance(casino_name), starting_balance - win);
    }
};

const name blackjack_tester::game_name = N(blackjack);
const name blackjack_tester::player_name = N(player);
const asset blackjack_tester::starting_balance = STRSYM("80000000.0000");

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
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("150.0000"));
    BOOST_REQUIRE_EQUAL(
        push_action(
            game_name,
            N(gameaction),
            {platform_name, N(gameaction)},
            mvo()("req_id", ses_id)("type", 0)("params", std::vector<param_t>{100'0000})
        ),
        success()
    );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(invalid_action, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("150.0000"));
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

BOOST_FIXTURE_TEST_CASE(invalid_decision, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("150.0000"));
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
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("150.0000"));
    bet(ses_id, STRSYM("100.0000"));

    push_cards(ses_id, {"Ad", "Ts", "2c", "7c"});
    signidice(game_name, ses_id);

    check_player_win(STRSYM("150.0000"));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(player_dealer_both_have_a_blackjack, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("150.0000"));
    bet(ses_id, STRSYM("100.0000"));

    push_cards(ses_id, {"Ad", "Ts", "Td", "As"});
    signidice(game_name, ses_id);

    check_player_win(STRSYM("0.0000"));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(dealer_has_a_blackjack, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("150.0000"));
    bet(ses_id, STRSYM("100.0000"));

    // Tc is a hole card
    push_cards(ses_id, {"Kd", "Ts", "Td"});
    signidice(game_name, ses_id);

    stand(ses_id);
    push_cards(ses_id, {"As"});
    signidice(game_name, ses_id);
    // dealer has Td As
    check_player_win(-STRSYM("150.0000"));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(player_hits_and_busts, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("150.0000"));
    bet(ses_id, STRSYM("100.0000"));

    push_cards(ses_id, {"Kd", "Ts", "Td"});
    signidice(game_name, ses_id);

    hit(ses_id);
    push_cards(ses_id, {"3d", "Jh"});
    signidice(game_name, ses_id);

    check_player_win(-STRSYM("100.0000"));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(player_hits_and_wins, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("150.0000"));
    bet(ses_id, STRSYM("100.0000"));

    push_cards(ses_id, {"Kd", "Ts", "Td"});
    signidice(game_name, ses_id);

    // player gets a 21 not a blackjack though
    hit(ses_id);
    push_cards(ses_id, {"Ac"});
    signidice(game_name, ses_id);

    stand(ses_id);

    // dealer gets 19
    push_cards(ses_id, {"9c", "4h", "5s"});
    signidice(game_name, ses_id);

    check_player_win(STRSYM("100.0000"));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(player_hits_two_times_and_wins, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("150.0000"));
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
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("150.0000"));
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
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("150.0000"));
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
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("150.0000"));
    bet(ses_id, STRSYM("100.0000"));

    push_cards(ses_id, {"6d", "6s", "Td"});
    signidice(game_name, ses_id);

    split(ses_id);
    push_cards(ses_id, {"8s", "Kh"});
    signidice(game_name, ses_id);

    const auto& state = get_state(ses_id);
    const cards_t active_cards{"6d", "8s"}, split_cards{"6s", "Kh"};
    BOOST_REQUIRE_EQUAL(state["player_cards"].as<cards_t>(), active_cards);
    BOOST_REQUIRE_EQUAL(state["split_cards"].as<cards_t>(), split_cards);
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(player_split_win_win, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("150.0000"));
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
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("150.0000"));
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
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("150.0000"));
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

BOOST_FIXTURE_TEST_CASE(player_split_doubles_down_both, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("150.0000"));
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
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("150.0000"));
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
    check_player_win(-STRSYM("600.0000"));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(player_split_double_blackjack, blackjack_tester) try {
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("150.0000"));
    bet(ses_id, STRSYM("100.0000"));

    push_cards(ses_id, {"Jd", "Js", "Td"});
    signidice(game_name, ses_id);

    split(ses_id);
    push_cards(ses_id, {"As", "Ad"});
    signidice(game_name, ses_id);

    stand(ses_id);

    stand(ses_id);
    push_cards(ses_id, {"7s"});
    signidice(game_name, ses_id);
    check_player_win(STRSYM("300.0000"));
} FC_LOG_AND_RETHROW()

#endif

BOOST_AUTO_TEST_SUITE_END()

} // namespace testing
