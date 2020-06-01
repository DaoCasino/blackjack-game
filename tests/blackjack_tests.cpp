#define TEST 1

#include <game_tester/game_tester.hpp>
#include <game_tester/strategy.hpp>
#include <fc/reflect/reflect.hpp>

#include "contracts.hpp"
#include <blackjack/card.hpp>

namespace testing {

using card_game::card;

class blackjack_tester : public game_tester {
};


BOOST_AUTO_TEST_SUITE(blackjack_tests)

BOOST_FIXTURE_TEST_CASE(new_game_test_min_deposit_fail, blackjack_tester)
{
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace testing
