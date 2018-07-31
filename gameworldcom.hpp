#include <eosiolib/eosio.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/types.hpp>
#include <eosiolib/action.hpp>
#include <eosiolib/symbol.hpp>
#include <eosiolib/time.hpp>
#include <eosiolib/singleton.hpp>
#include <math.h>

using namespace eosio;
using namespace std;

const uint32_t gap = 24 * 60 * 60;
const uint32_t gap_delta = 30;
const uint64_t base = 1000ll * 1000;
const account_name contract_fee_account = N(gameworldfee);
const uint64_t key_precision = 100;
const uint8_t RED = 0;
const uint8_t BLUE = 1;
const uint8_t PROFITSPLIT[2] = {60, 30};
const uint8_t POTSPLIT[2] = {60, 30};

class gameworldcom: public contract {
public:
    gameworldcom(account_name self)
            : contract(self),
              sgt_round(_self, _self)
    {};
    void transfer(account_name from, account_name to, asset quantity, string memo);
    void withdraw(account_name to);
    void create(time_point_sec start);
private:

    // @abi table round i64
    struct st_round {
        account_name player;
        uint8_t team;
        bool ended;
        time_point_sec end;
        uint64_t red;
        uint64_t blue;
        uint64_t key;
        uint64_t eos;
        uint64_t pot;
        uint64_t mask;
        uint64_t redmask;
        uint64_t bluemask;
        time_point_sec start;
    };
    typedef singleton<N(round), st_round> tb_round;
    tb_round sgt_round;

    // @abi table player i64
    struct st_player {
        account_name affiliate_name;
        uint64_t aff_vault;
        uint64_t pot_vault;
        uint64_t red;
        uint64_t blue;
        uint64_t key;
        uint64_t eos;
        uint64_t mask;
    };
    typedef singleton<N(player), st_player> tb_player;

    uint64_t buy_keys(uint64_t eos) {
        st_round round = get_round();
        return key(round.eos + eos) - key(round.eos);
    };

    uint64_t key(uint64_t eos) {
        return key_precision * (sqrt(eos * 1280000 + 230399520000) - 479999);
    }
    st_round get_round() {
        eosio_assert(sgt_round.exists(), "round not exist");
        return sgt_round.get();
    }
};

#define EOSIO_ABI_EX( TYPE, MEMBERS ) \
extern "C" { \
    void apply( uint64_t receiver, uint64_t code, uint64_t action ) { \
        auto self = receiver; \
        if( action == N(onerror)) { \
            /* onerror is only valid if it is for the "eosio" code account and authorized by "eosio"'s "active permission */ \
            eosio_assert(code == N(eosio), "onerror action's are only valid from the \"eosio\" system account"); \
        } \
        if( ((code == self && action != N(transfer)) || action == N(onerror)) || (code == N(eosio.token) && action == N(transfer)) ) { \
            TYPE thiscontract( self ); \
            switch( action ) { \
                EOSIO_API( TYPE, MEMBERS ) \
            } \
         /* does not allow destructor of thiscontract to run: eosio_exit(0); */ \
        } \
    } \
} \

EOSIO_ABI_EX(gameworldcom, (withdraw) (transfer) (create))