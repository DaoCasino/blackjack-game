#define TEST 1

#include <game_tester/game_tester.hpp>
#include <game_tester/strategy.hpp>
#include <fc/reflect/reflect.hpp>

#include "contracts.hpp"
#include <blackjack/card.hpp>

namespace testing {

using card_game::card;

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

    fc::variant get_bet(name game_name, uint64_t ses_id) {
        vector<char> data = get_row_by_account(game_name, game_name, N(bet), ses_id);
        return data.empty() ? fc::variant()
                            : abi_ser[game_name].binary_to_variant("bet_row", data, abi_serializer_max_time);
    }
};

const name blackjack_tester::game_name = N(blackjack);
const name blackjack_tester::player_name = N(player);
const asset blackjack_tester::starting_balance = STRSYM("80000000.0000");

BOOST_AUTO_TEST_SUITE(blackjack_tests)

BOOST_FIXTURE_TEST_CASE(new_game_test_min_deposit_fail, blackjack_tester)
{
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
}

BOOST_FIXTURE_TEST_CASE(new_game_test_min_deposit_ok, blackjack_tester)
{
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
}

BOOST_FIXTURE_TEST_CASE(new_game_test_max_deposit_fail, blackjack_tester)
{
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
}

BOOST_FIXTURE_TEST_CASE(new_game_test_max_deposit_ok, blackjack_tester)
{
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
}

BOOST_FIXTURE_TEST_CASE(bet_action, blackjack_tester)
{
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("200.0000"));
    BOOST_REQUIRE_EQUAL(
        push_action(
            game_name,
            N(gameaction),
            {platform_name, N(gameaction)},
            mvo()("req_id", ses_id)("type", 0)("params", std::vector<param_t>{100'0000})
        ),
        success()
    );
}

BOOST_FIXTURE_TEST_CASE(invalid_action, blackjack_tester)
{
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("200.0000"));
    BOOST_REQUIRE_EQUAL(
        push_action(
            game_name,
            N(gameaction),
            {platform_name, N(gameaction)},
            mvo()("req_id", ses_id)("type", 2)("params", std::vector<param_t>{0})
        ),
        wasm_assert_msg("invalid action")
    );
}

BOOST_FIXTURE_TEST_CASE(invalid_decision, blackjack_tester)
{
    const auto ses_id = new_game_session(game_name, player_name, casino_id, STRSYM("200.0000"));
    bet(ses_id, STRSYM("100.0000"));
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
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace testing
