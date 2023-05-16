_Pragma("once")
#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>

using namespace eosio;

namespace swapspace
{
    struct token{
        name contract;
        symbol symbol;
    };

    struct extended_symbol{
        symbol symbol;
        name contract;
    };

    struct pooltoken{
        uint32_t weight;
        extended_symbol symbol;
        asset reserve;
    };

    struct [[eosio::table]] total_data
    {
        uint64_t id;
        asset total_minor;
        asset total_major;
        uint64_t total_mmf = 0;
        asset total_fee;
        asset history_mm_minor;   
        asset history_mm_major;   
        asset history_bank_minor;
        asset history_bank_major;
        asset history_fee;
        uint32_t time;

        uint64_t primary_key() const { return 0; }
    };

    //dfs
    struct [[eosio::table]] market_data{
        uint64_t mid;
        name contract0;
        name contract1;
        symbol sym0;
        symbol sym1;   
        asset reserve0;
        asset reserve1;
        uint64_t liquidity_token;
        double price0_last;
        double price1_last;
        uint64_t price0_cumulative_last;
        uint64_t price1_cumulative_last;
        time_point_sec last_update;

        uint64_t primary_key() const { return mid; }       
    };

    //box 
    struct [[eosio::table]] pair_data{
        uint64_t id;
        token token0;
        token token1;
        asset reserve0;
        asset reserve1;
        uint64_t liquidity_token;
        double price0_last;
        double price1_last;
        uint64_t price0_cumulative_last;
        uint64_t price1_cumulative_last;
        time_point_sec block_time_last;

        uint64_t primary_key() const { return id; }       
    };

    //dop pools
    struct [[eosio::table]] pool_data
    {
        uint64_t id;
        symbol_code code;
        uint16_t swap_fee;
        uint64_t total_lptoken;
        uint32_t create_time;
        uint32_t last_update_time;
        std::vector<pooltoken> tokens;

        uint64_t primary_key() const { return 0; }
    };


    //rome
    struct [[eosio::table]] rome_markets_data{
        uint64_t market_id;
        token coin0;
        token coin1;
        asset reserve0;
        asset reserve1;
        uint64_t liquidity_token;
        double price0_last;
        double price1_last;
        time_point_sec last_update;

        uint64_t primary_key() const { return market_id; }       
    };

    //dfs
    typedef eosio::multi_index<"markets"_n, market_data> market;

    //pizza
    typedef eosio::multi_index<"total"_n, total_data> total_das;
    
    //box
    typedef eosio::multi_index<"pairs"_n, pair_data> pair;

    //dop
    typedef eosio::multi_index<"pools"_n, pool_data> doppool_das; 

    //rome
    typedef eosio::multi_index<"markets"_n, rome_markets_data> rome_markets;
}