#include "gameworldcom.hpp"

void gameworldcom::transfer(account_name from, account_name to, asset quantity, string memo) {
    if (from == _self || to != _self) {
        return;
    }
    eosio_assert(quantity.symbol == S(4,EOS), "gameworldcom only accepts EOS");
    eosio_assert(quantity.is_valid(), "Invalid token transfer");
    eosio_assert(quantity.amount > 0, "Quantity must be positive");

    memo.erase(memo.begin(), find_if(memo.begin(), memo.end(), [](int ch) {
        return !isspace(ch);
    }));
    memo.erase(find_if(memo.rbegin(), memo.rend(), [](int ch) {
        return !isspace(ch);
    }).base(), memo.end());

    auto separator_pos = memo.find(' ');
    if (separator_pos == string::npos) {
        separator_pos = memo.find('-');
    }
    string team_str;
    account_name refer_account = 0;

    if (separator_pos != string::npos) {
        team_str = memo.substr(0, separator_pos);
        string refer_account_name_str = memo.substr(separator_pos + 1);
        eosio_assert(refer_account_name_str.length() <= 12, "account name can only be 12 chars long");
        refer_account = string_to_name(refer_account_name_str.c_str());
        tb_player refer_player_sgt(_self, refer_account);
        if (!refer_player_sgt.exists()) {
            refer_account = 0;
        }
    } else {
        team_str = memo;
    }

    eosio_assert((team_str == "red" || team_str == "blue"), "team must be red or blue");
    uint8_t team = (team_str == "red") ? RED : BLUE;

    st_round round = get_round();
    eosio_assert(time_point_sec(now()) < round.end, "this round is ended");

    // cal fee
    uint64_t contract_fee = 2 * quantity.amount / 100;
    uint64_t refer_fee = 8 * quantity.amount / 100;

    // buy key
    st_player default_player = st_player{
        .red = 0,
        .blue = 0,
        .key = 0,
        .eos = 0,
        .mask = 0,
        .affiliate_name = refer_account,
        .aff_vault = 0,
        .pot_vault = 0,
    };
    tb_player players(_self, from);
    st_player player = players.get_or_create(from, default_player);

    uint64_t keys = buy_keys(quantity.amount);
    eosio_assert(keys >= key_precision * 100, "amount of key should be bigger than 100");
    player.eos += quantity.amount;
    player.key += keys;

    round.player = from;
    round.team = team;
    round.eos += quantity.amount;
    round.key += keys;
    eosio_assert(round.key >= keys, "amount of key overflow");

    time_point_sec latest = time_point_sec(now() + gap);
    round.end = min(round.end + (gap_delta * (keys / (key_precision * 100))), latest);

    if (team == RED) {
        player.red += keys;
        round.red += keys;
    } else {
        player.blue += keys;
        round.blue += keys;
    }

    // profit
    uint64_t base_profit = quantity.amount * PROFITSPLIT[team] / 100;
    uint64_t profit_per_key = base_profit * base / round.key;
    round.mask += profit_per_key;
    eosio_assert(round.mask >= profit_per_key, "mask overflow");

    uint64_t player_profit = profit_per_key * keys / base;
    player.mask += round.mask * keys / base - player_profit;

    uint64_t total_profit = profit_per_key * round.key / base;
    eosio_assert(total_profit <= base_profit, "final result of total profit shouldn't be bigger than base profit");

    uint64_t total_pot = quantity.amount - contract_fee - refer_fee - total_profit;
    eosio_assert(total_pot >= quantity.amount * (100 - PROFITSPLIT[team] - 2 - 8) / 100, "something wrong with final result of total pot");

    round.pot += total_pot;
    eosio_assert(round.pot >= total_pot, "pot oeverflow");

    // save player and round
    players.set(player, from);
    sgt_round.set(round, _self);

    // refer fee
    if (player.affiliate_name != 0) {
        tb_player refer_player_sgt(_self, player.affiliate_name);
        eosio_assert(refer_player_sgt.exists(), "refer player not exist");

        st_player affilicate_player = refer_player_sgt.get();
        affilicate_player.aff_vault += refer_fee;
        eosio_assert(affilicate_player.aff_vault >= refer_fee, "affilicate fee overflow");

        refer_player_sgt.set(affilicate_player, player.affiliate_name);
    } else {
        contract_fee += refer_fee;
    }

    // contract fee
    asset contract_fee_asset(contract_fee, S(4,EOS));
    action(
            permission_level{ _self, N(active) },
            N(eosio.token),
            N(transfer),
            make_tuple(_self, contract_fee_account, contract_fee_asset, string(""))
    ).send();
}

void gameworldcom::withdraw(account_name to) {
    eosio_assert(has_auth(to) || has_auth(_self), "invalid auth");
    st_round round = get_round();

    if (time_point_sec(now()) > round.end && !round.ended) {
        round.ended = true;
        uint64_t contract_fee = round.pot * 10 / 100;
        uint64_t win = round.pot * POTSPLIT[round.team] / 100;
        uint64_t team_profit = round.pot - contract_fee - win;
        if (round.team == RED) {
            uint64_t profit_per_key = team_profit * base / round.red;
            round.redmask += profit_per_key;
        } else {
            uint64_t profit_per_key = team_profit * base / round.blue;
            round.bluemask += profit_per_key;
        }
        sgt_round.set(round, _self);

        tb_player sgt_winner(_self, round.player);
        eosio_assert(sgt_winner.exists(), "winner not exist");
        st_player winner = sgt_winner.get();
        winner.pot_vault += win;
        sgt_winner.set(winner, round.player);
    }

    // cal profit
    tb_player sgt_player(_self, to);
    eosio_assert(sgt_player.exists(), "player not exists");
    st_player player = sgt_player.get();
    uint64_t profit = round.mask * player.key / base - player.mask;
    if (profit > 0) {
        player.mask += profit;
    }
    uint64_t vault = profit + player.aff_vault + player.pot_vault;

    if (round.ended) {
        vault += player.red * round.redmask / base + player.blue * round.bluemask / base;
        sgt_player.remove();
    } else {
        player.aff_vault = 0;
        player.pot_vault = 0;
        sgt_player.set(player, to);
    }
    eosio_assert(vault < round.eos, "amount of withdraw should be less than eos of this round");

    // transfer
    if (vault > 0) {
        asset vault_asset(vault, S(4,EOS));
        action(
                permission_level{ _self, N(active) },
                N(eosio.token),
                N(transfer),
                make_tuple(_self, to, vault_asset, string("gameworldcom withdraw"))
        ).send();
    }
}