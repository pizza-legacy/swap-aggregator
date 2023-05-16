#include "aggregator.hpp"
#include <eosio/system.hpp>
#include <eosio/transaction.hpp>
#include <eosio/crypto.hpp>
#include "account.hpp"
#include "swap.hpp"
#include <math.h>

#define TRANSFER name("transfer")
#define ACTIVE name("active")
#define EOS2USDE name("eos2usde")
#define EOSIOTOKEN name("eosio.token")

#define EOS_SYMBOL symbol("EOS",4)
#define USDE_SYMBOL symbol("USDE",4)
#define EOS_SYMBOL_NAME "eos"
#define USDE_SYMBOL_NAME "usde"

#define FEE_RATE name("feerate")
const double DEFAULT_FEE_RATE = 0;
#define NIL "nil"
constexpr uint32_t SECONDS_PER_MINUTE = 60;
constexpr uint32_t SECONDS_PER_HOUR = SECONDS_PER_MINUTE * 60;
constexpr uint32_t SECONDS_PER_DAY = 24 * SECONDS_PER_HOUR;

swap_aggregator::swap_aggregator(name receiver, name code, datastream<const char *> ds):contract(receiver, code, ds),m_mix_das(_self,_self.value),\
m_pause_das(_self, _self.value),
m_check_das(_self, _self.value),
m_path_das(_self, _self.value){
    auto it_status = m_pause_das.find(0);
    if (it_status == m_pause_das.end())
    {
        m_pause_das.emplace(_self, [&](auto &das) {
            das.id = 0;
            das.status = 0;
        });
    }
}


void swap_aggregator::pause(uint32_t status){
    require_auth(eosio::permission_level{LOCAL_USER,ACTIVE});
    check((status == 0) || (status == 1), "pause table status is error") ;

    auto it_status = m_pause_das.find(0);
    if(it_status != m_pause_das.end()){
        m_pause_das.modify(it_status, eosio::name(0), [&](auto& das){
            das.status = status;
        });
    } else{
        m_pause_das.emplace(_self, [&](auto& das){
            das.id = 0;
            das.status = 0;
        });
    }
}

void swap_aggregator::set_local_operation(){
    verify();
#ifndef DEBUG
    require_auth(eosio::permission_level{LOCAL_USER,ACTIVE});
#endif // !DEBUG
}

void swap_aggregator::verify()
{
    auto it_status = m_pause_das.find(0);
    check(it_status != m_pause_das.end(), "cant't find pause");
    check(it_status->status == 0, "contract is stop");
}

[[eosio::on_notify("*::transfer")]] void swap_aggregator::transfer(name from, name to, asset quantity, std::string memo) {
    verify();
    require_auth(from);
    check(quantity.amount > 0, "transfer Quantity must be positive");
    check(memo.size() <= 256, "transfer memo has more than 256 bytes");
    check(quantity.is_valid(), "transfer Invalid asset");
    if (quantity.symbol == EOS_SYMBOL){
        check(get_first_receiver() == EOSIOTOKEN, "eos contract must be eosio.token");
    }

    if (from == _self || to != _self)
    {
        return;
    }

#ifdef DEBUG
        print_f("memo:%,from:%\n",memo,from);
#endif    

    if ((from == BOX_ACCOUNT && memo == "Defibox: swap token") || (from == DFS_ACCOUNT && memo == "swap success") \
    || (from == PIZZA_MARKET_ACCOUNT  && (memo == "swap minor" || memo == "swap major"))\
    ||(from == DOP_ACCOUNT && memo == "DolphinSwap: swap token") || (from == ROME_ACCOUNT && memo == "swap success")){    
        handle_mix(from, quantity);
    }else{
        if (memo != "")
        {
            std::vector<std::string> multi_path;
            SplitString(memo, multi_path, "|");
            auto size = multi_path.size();

            if (size == 1){

                exchange(from, quantity, memo);
            }else{

                check(size == 2, "not support to much path");
                m_path_das.emplace(_self, [&](auto &row) {
                    row.id = m_path_das.available_primary_key();
                    row.account = from;
                    row.trx_id = tx_hash();
                    row.total_path = size;
                    row.current_path = 0;
                    row.memo = memo;
                });
//                check(size < 0, "size must > 0");
                exchange(_self, quantity, multi_path[0]);
            }

        }
    }
}

std::string swap_aggregator::string_join(std::vector<std::string> &str, std::string split){
    check(str.size() > 1, "str size must be  greater than one");
    std::string return_str;
    for (auto s:str){
        return_str += s;
        return_str += split;
    }

    return_str.pop_back();
    return return_str;
}

double swap_aggregator::get_fee_rate(const asset &quantity){
    std::string sym = quantity.symbol.code().to_string();
    transform(sym.begin(),sym.end(),sym.begin(),::tolower);
    parameter_das my_parameter_das(_self, name{sym}.value);
    auto it_fee_rate = my_parameter_das.find(FEE_RATE.value);
    double fee_rate = DEFAULT_FEE_RATE;
    if (it_fee_rate != my_parameter_das.end()){
        fee_rate = get_split_price(it_fee_rate->value);
    }

    return fee_rate;
}

auto swap_aggregator::getnewpool(uint64_t box_id, uint64_t dfs_id, uint64_t dop_id, uint64_t rome_id,const std::string &pizza_memo,const asset &quantity, std::map<name,newpool> &r){
    uint64_t first_total_amount = 0;
    uint64_t second_total_amount = 0;
    symbol second_sym;
    bool is_set_second_sym = false;
    swapspace::pair box_pair(BOX_ACCOUNT, BOX_ACCOUNT.value);
    std::set<symbol> symbols;
    auto itr_pair = box_pair.find(box_id);
    if(itr_pair != box_pair.end()){
        asset first_quantity;
        asset second_quantity;
        check(itr_pair->token0.symbol == quantity.symbol || itr_pair->token1.symbol == quantity.symbol, "box id is error");
        symbols.insert(itr_pair->token0.symbol);
        symbols.insert(itr_pair->token1.symbol);
        newpool p;
        if (itr_pair->token0.symbol == quantity.symbol){
            first_quantity = itr_pair->reserve0;
            second_quantity = itr_pair->reserve1;             
        }else{
            first_quantity = itr_pair->reserve1;
            second_quantity = itr_pair->reserve0;
        }

        second_sym = second_quantity.symbol;
        is_set_second_sym = true;
        p.first_amount = first_quantity.amount;
        p.second_amount = second_quantity.amount;
        first_total_amount += first_quantity.amount;
        second_total_amount += second_quantity.amount;
        r[BOX_ACCOUNT] = p;
    }
 
    swapspace::market dfs_market(DFS_ACCOUNT, DFS_ACCOUNT.value);
    auto it_market = dfs_market.find(dfs_id);
    if(it_market != dfs_market.end()){
        asset first_quantity;
        asset second_quantity;
        check(it_market->sym0 == quantity.symbol || it_market->sym1 == quantity.symbol, "dfs id is error");
        symbols.insert(it_market->sym0);
        symbols.insert(it_market->sym1);
        newpool p;
        if (it_market->sym0 == quantity.symbol){
            first_quantity = it_market->reserve0;
            second_quantity = it_market->reserve1;
        }else{
            first_quantity = it_market->reserve1;
            second_quantity = it_market->reserve0;
        }

        if (is_set_second_sym){
            check(second_sym == second_quantity.symbol,"second symbol is error");
        }else{
            second_sym = second_quantity.symbol;
            is_set_second_sym = true;
        }

        p.first_amount = first_quantity.amount;
        p.second_amount = second_quantity.amount;
        first_total_amount += first_quantity.amount;
        second_total_amount += second_quantity.amount;
        r[DFS_ACCOUNT] = p;
    }

    name pair;
    if ((quantity.symbol == EOS_SYMBOL && pizza_memo == USDE_SYMBOL_NAME) || (quantity.symbol == USDE_SYMBOL && pizza_memo == EOS_SYMBOL_NAME)){
        pair = EOS2USDE;
    }else{
        if (pizza_memo != "eos"){
            pair = name{pizza_memo + "2eos"};
        }else{
            std::string q = quantity.symbol.code().to_string();
            transform(q.begin(),q.end(),q.begin(),::tolower);
            pair = name{ q+ "2eos"};
        }   
    }

#ifdef DEBUG
    print_f("pizza pair:%\n",pair);
#endif
    swapspace::total_das my_total(PIZZA_SWAP_ACCOUNT, pair.value);
    auto it_total = my_total.find(0);
    if (it_total != my_total.end()){
        asset first_quantity;
        asset second_quantity;
        check(it_total->total_major.symbol == quantity.symbol || it_total->total_minor.symbol == quantity.symbol, "dfs id is error");
        symbols.insert(it_total->total_major.symbol);
        symbols.insert(it_total->total_minor.symbol);
        
        newpool p;
        if (it_total->total_major.symbol == quantity.symbol){
            first_quantity = it_total->total_major;
            second_quantity = it_total->total_minor;
        }else{
            first_quantity = it_total->total_minor;;
            second_quantity = it_total->total_major;
        }

        if (is_set_second_sym){
            check(second_sym == second_quantity.symbol,"second symbol is error");
        }else{
            second_sym = second_quantity.symbol;
            is_set_second_sym = true;
        }

        p.first_amount = first_quantity.amount;
        p.second_amount = second_quantity.amount;
        first_total_amount += first_quantity.amount;
        second_total_amount += second_quantity.amount;
        r[PIZZA_SWAP_ACCOUNT] = p;
    }

    swapspace::doppool_das my_doppool_das(DOP_ACCOUNT, DOP_ACCOUNT.value);
    auto it_pool = my_doppool_das.find(dop_id);
    if(it_pool != my_doppool_das.end()){
        asset first_quantity;
        asset second_quantity;
        check(it_pool->tokens[0].reserve.symbol == quantity.symbol || it_pool->tokens[1].reserve.symbol == quantity.symbol, "dfs id is error");
        symbols.insert(it_pool->tokens[0].reserve.symbol);
        symbols.insert(it_pool->tokens[1].reserve.symbol);
        
        newpool p;
        uint32_t weight1;
        uint32_t weight2;
        if (it_pool->tokens[0].reserve.symbol == quantity.symbol){
            first_quantity = it_pool->tokens[0].reserve;
            second_quantity = it_pool->tokens[1].reserve;
            weight1 = it_pool->tokens[0].weight;
            weight2 = it_pool->tokens[1].weight;
        }else{
            first_quantity = it_pool->tokens[1].reserve;
            second_quantity = it_pool->tokens[0].reserve;
            weight1 = it_pool->tokens[1].weight;
            weight2 = it_pool->tokens[0].weight;
        }

        if (is_set_second_sym){
            check(second_sym == second_quantity.symbol,"second symbol is error");
        }else{
            second_sym = second_quantity.symbol;
            is_set_second_sym = true;
        }

        p.first_amount = first_quantity.amount * get_weight_rate(weight1);
        p.second_amount = second_quantity.amount * get_weight_rate(weight2);;
        first_total_amount += p.first_amount;
        second_total_amount += p.second_amount;
        r[DOP_ACCOUNT] = p;
    }

    swapspace::rome_markets my_rome_markets(ROME_ACCOUNT, ROME_ACCOUNT.value);
    auto it_rome = my_rome_markets.find(rome_id);
    if (it_rome != my_rome_markets.end()){
        asset first_quantity;
        asset second_quantity;
        check(it_rome->coin0.symbol == quantity.symbol || it_rome->coin1.symbol == quantity.symbol, "rome id is error");
        symbols.insert(it_rome->coin0.symbol);
        symbols.insert(it_rome->coin1.symbol);
        
        newpool p;
        if (it_rome->coin0.symbol == quantity.symbol){
            first_quantity = it_rome->reserve0;
            second_quantity = it_rome->reserve1;             
        }else{
            first_quantity = it_rome->reserve1;
            second_quantity = it_rome->reserve0;
        }

        second_sym = second_quantity.symbol;
        if (is_set_second_sym){
            check(second_sym == second_quantity.symbol,"second symbol is error");
        }else{
            second_sym = second_quantity.symbol;
            is_set_second_sym = true;
        }

        p.first_amount = first_quantity.amount;
        p.second_amount = second_quantity.amount;
        first_total_amount += first_quantity.amount;
        second_total_amount += second_quantity.amount;
        r[ROME_ACCOUNT] = p;
    }

    std::string err = "id is error box_id:" + std::to_string(box_id) + " dfs_id:" + std::to_string(dfs_id) + " dop_id:" \
    + std::to_string(dop_id)+ " rome_id:" + std::to_string(rome_id) + ", " + pizza_memo;
    check(symbols.size() == 2, err.c_str());
    return std::make_tuple(first_total_amount,second_total_amount, second_sym);
}

double swap_aggregator::get_weight_rate(uint32_t weight){
    return (1 - (double)weight / 100) / 0.5;
}

auto swap_aggregator::new_distribute(const std::map<name,newpool> & pools, uint64_t first_total_amount, uint64_t second_total_amount, const asset &quantity){
    std::map<name,asset> all_quantity;
    std::string all_err;
    uint64_t all_token_amount = 0;

    double k = (double)first_total_amount * (double)second_total_amount;
    check(k > 0, "k must be positive");
    double before_price = (double)second_total_amount / (double)first_total_amount;

    double temp = (double)first_total_amount+(double)quantity.amount * 0.997;
    double after_price = k / temp / temp;

    all_err += "quantity -> " + std::to_string(quantity.amount) + " ";
    all_err += "bPrice -> " + std::to_string(before_price) + " ";
    all_err += "aPrice -> " + std::to_string(after_price) + " ";

    for(auto pool : pools){
        double fair_price =  before_price * after_price /((double)pool.second.second_amount / (double)pool.second.first_amount);
        double after_trade_first_amount = (((double)pool.second.first_amount/10000) * ((double)pool.second.second_amount/10000))/fair_price;
        all_err += "before ->" + std::to_string(after_trade_first_amount) + " ";

        after_trade_first_amount = pow(after_trade_first_amount, 0.5);
        all_err += "calculate ->" + std::to_string(after_trade_first_amount) + " ";
        after_trade_first_amount *= 10000;
        all_err += "after ->" + std::to_string(after_trade_first_amount) + " ";

        uint64_t distribute_amount = after_trade_first_amount - pool.second.first_amount;
#ifdef DEBUG
        print_f("pool.first:%,mafter_trade_first_amount:%,pool.second.first_amount:%,distribute_amount:%\n",pool.first,after_trade_first_amount,pool.second.first_amount, distribute_amount);
#endif
        all_err += (pool.first.to_string() + std::to_string(distribute_amount))+ ":" + std::to_string(after_trade_first_amount) + ":" + std::to_string(fair_price) + ":" + std::to_string(pool.second.first_amount) + ":" +std::to_string(pool.second.second_amount);
        if (distribute_amount <= 0){
            continue;
        }

        all_quantity[pool.first] = asset(distribute_amount, quantity.symbol);
        all_token_amount += distribute_amount;
    }

    all_err += "->" + std::to_string(all_token_amount);
//    check(false,all_err.c_str());
    return std::make_tuple(all_quantity,all_token_amount);
}

// Q =  Y - K/(X+P)
// abs((Qn/Pn)/(Yn/Xn) - 1)
void swap_aggregator::new_calculate_dk(uint64_t first, uint64_t second, const asset &p, double wave, const name &channel){
    uint64_t x,y;
    x = first;
    y = second;
    uint64_t q = (y - (double)y * (double)x/(double)(x + p.amount)) * 0.997;
    double rate = (q/(double)p.amount)  / ((double)y/(double)x)- 1;
    rate = rate < 0 ? -rate:rate;
    rate = uint64_t(rate*10000);
    rate /= 10000;
    std::string err =channel.to_string() +" slippage too high ,must less than :" + std::to_string(wave) + ", transfer amount:" + std::to_string(p.amount);
    check(rate <= wave, err.c_str());
}

//14-14-eos-5,2
void swap_aggregator::exchange(const name &account, const asset &quantity, std::string &memo){
     std::vector<std::string> patterms;
    SplitString(memo, patterms, ";");
    if (patterms.size() <= 2){
        return;
    }
    
    double wave = 0;
    if (patterms.size() == 5){
        wave = get_split_price(patterms[4]);
    }else if (patterms.size() == 6){
        wave = get_split_price(patterms[5]);
    }

    std::map<name,std::string> channel_string;
    uint64_t box_id = 0;
    uint64_t dfs_id = 0;
    uint64_t dop_id = 0;
    uint64_t rome_id = 0;
    uint64_t residual_amount = quantity.amount;
    if (patterms[0] != NIL){
        std::vector<std::string> box_excision;
        box_excision.push_back("swap");
        box_excision.push_back("0");
        box_excision.push_back(patterms[0]);
        channel_string[BOX_ACCOUNT] = string_join(box_excision, ",");
        std::vector<std::string> box_patterms;
        SplitString(patterms[0], box_patterms, "-");
        box_id = stol(box_patterms[box_patterms.size()-1]);
    }

    if (patterms[1] != NIL){
        std::vector<std::string> dfs_excision;
        dfs_excision.push_back("swap");
        dfs_excision.push_back(patterms[1]);
        dfs_excision.push_back("0");
        dfs_excision.push_back("38");
        channel_string[DFS_ACCOUNT]  = string_join(dfs_excision, ":");
        std::vector<std::string> dfs_patterms;
        SplitString(patterms[1], dfs_patterms, "-");
        dfs_id = stol(dfs_patterms[dfs_patterms.size()-1]);     
    }

    std::string pizza_swap_sym = NIL;
    if (patterms[2] != NIL ){
        channel_string[PIZZA_SWAP_ACCOUNT] = patterms[2] + "-newswap";
        pizza_swap_sym = patterms[2];
    }

    if (patterms[3] != NIL){
        std::vector<std::string> dop_excision;
        dop_excision.push_back("swap");
        dop_excision.push_back(patterms[3]);
        channel_string[DOP_ACCOUNT] = string_join(dop_excision, ":");
        std::vector<std::string> dop_patterms;
        SplitString(patterms[3], dop_patterms, "-");
        dop_id = stol(dop_patterms[dop_patterms.size()-1]);
    }

    if (patterms.size() > 5){
        if (patterms[4] != NIL){
            std::vector<std::string> rome_excision;
            rome_excision.push_back("swap");
            rome_excision.push_back("0");
            rome_excision.push_back(patterms[4]);
            channel_string[ROME_ACCOUNT] = string_join(rome_excision, ":");
            std::vector<std::string> rome_patterms;
            SplitString(patterms[4], rome_patterms, "-");
            rome_id = stol(rome_patterms[rome_patterms.size()-1]);
        }
    }

    std::map<name,newpool> r;
    uint64_t first_total_amount = 0;
    uint64_t second_total_amount = 0;
    symbol second_sym;
    std::tie(first_total_amount, second_total_amount,second_sym) = getnewpool(box_id,dfs_id,dop_id, rome_id,pizza_swap_sym,quantity,r);
    double fee_rate = get_fee_rate(quantity);

    bool is_eos = false;
    if (quantity.symbol == EOS_SYMBOL || (second_sym != EOS_SYMBOL && quantity.symbol != EOS_SYMBOL)){
        is_eos = true;
    }

    std::map<name,asset> all_quantity;
    std::map<name,asset> transfer_quantity;
    uint64_t all_token_amount;
    uint64_t rediual_amount = quantity.amount;
    std::tie(all_quantity,all_token_amount) = new_distribute(r, first_total_amount,second_total_amount, quantity);
    check(all_token_amount > 0, "all_token_amount must be positive");
    for (auto q: all_quantity){
#ifdef DEBUG
        print_f("q.second.amount:%,all_token_amount:%,quantity.amount:%\n",q.second.amount,all_token_amount,quantity.amount);
#endif  
        asset transfer = asset((double)q.second.amount /(double)all_token_amount *(double)quantity.amount,quantity.symbol);
        rediual_amount -= transfer.amount;
        transfer_quantity[q.first] = transfer;
    }

    uint64_t transfer_times = 0;  
    auto total_fee = asset(0, quantity.symbol); 
    asset total_transfer_quantity = asset(0, quantity.symbol);
    for(auto t : transfer_quantity){
        asset transfer = t.second;
        transfer_times++;
        if (rediual_amount > 0){
            transfer.amount += rediual_amount;
            rediual_amount = 0;
        }

        if (wave > 0){
            auto it_pool = r.find(t.first);
            new_calculate_dk(it_pool->second.first_amount,it_pool->second.second_amount, transfer, wave, t.first);
        }

        std::string transfer_string;
        auto it_transfer_string = channel_string.find(t.first);
        check(it_transfer_string != channel_string.end(), "can't find channel string");
        if (is_eos){
            auto fee = asset((double)transfer.amount * fee_rate, quantity.symbol);
            if (t.first == PIZZA_SWAP_ACCOUNT){
                fee.amount = 0;
            }

            transfer -= fee;
            total_fee += fee;
        }

        inline_transfer(_self, t.first, transfer,it_transfer_string->second,get_first_receiver());
        total_transfer_quantity += transfer;
    }

    if (total_fee.amount > 0 && is_eos){
        inline_transfer(_self, CHARGE_ACCOUNT, total_fee,std::string("aggregator front fee"),get_first_receiver());
    }

    std::string rediual_amount_err = "rediual_amount must be zero, " +std::to_string(rediual_amount);
    check(rediual_amount == 0, rediual_amount_err.c_str());
    check(transfer_times > 0, "transfer quantity must be positive");
    auto timestamp = current_time_point().time_since_epoch().count();
    auto id = find_id(m_mix_das, timestamp / 1000);
    total_transfer_quantity += total_fee;
    std::string err ="transfer quantity is :" + std::to_string(total_transfer_quantity.amount);
    check(total_transfer_quantity == quantity, err.c_str());
    m_mix_das.emplace(_self, [&](auto &das) {
        das.id = id;
        das.account = account;
        das.send = quantity;
        if (is_eos){
            das.fee = total_fee;
        }else{
            das.fee = asset(0,EOS_SYMBOL);
        }
        
        das.start_time = now();
        das.trx_id = tx_hash();
        das.transfer_times = transfer_times;
        das.received = asset(0,quantity.symbol);
    });
}

void swap_aggregator::trim(std::string &s){
    int index = 0;
    if( !s.empty())
    {
        while( (index = s.find(' ',index)) != std::string::npos)
        {
            s.erase(index,1);
        }
    }
}

double swap_aggregator::get_split_price(std::string desc){ 
    std::vector<std::string> price_descs;
    SplitString(desc,price_descs, ",");
    check(price_descs.size() > 1, "price must contain ,");
    auto price = stoll(price_descs[0]);
    std::string str_err = "price must greater than zero "+ desc+" "+price_descs[1] +" price_descs:" + price_descs[0] + " int :" + std::to_string(price);
    check(price >= 0, str_err.c_str());
    return  get_price(price_descs[0], price_descs[1]);   
}

double swap_aggregator::get_price(std::string molecule, std::string denominator){
    return stoll(molecule) / pow(10,stoll(denominator));
}

void swap_aggregator::handle_mix(const name &account, const asset &quantity){
    uint32_t path_index = 0;

    auto path_table = m_path_das.get_index<name("trxid")>();
    auto path = path_table.find(tx_hash());
    if (path != path_table.end()){
        path_index = path->current_path;
    }

    auto id = get_mix_id(0);
    auto it_mix = m_mix_das.find(id);
    check(it_mix != m_mix_das.end(), "handle_mix can't find mix id");
    auto transfer_quantity = quantity;
    if (quantity.symbol ==  EOS_SYMBOL){
        if (account == BOX_ACCOUNT || account == DFS_ACCOUNT || account == DOP_ACCOUNT){
            auto fee_rate = get_fee_rate(quantity);
            auto fee = asset((double)quantity.amount * fee_rate, quantity.symbol);
            transfer_quantity = quantity - fee;

            m_mix_das.modify(it_mix, eosio::name(0), [&](auto& das){
                das.fee =  asset(das.fee.amount + fee.amount, fee.symbol);
            });
        }
    }else{
        auto it_check =  m_check_das.find(id);
        if (it_check != m_check_das.end()){
            check(get_first_receiver() == it_check->contract, "contract is error");
        }else{
            m_check_das.emplace(_self, [&](auto &das) {
                das.id = id;
                das.contract = get_first_receiver();
            });
        }
    }

#ifdef DEBUG
    print_f("quantity:%,account:%,it_mix->transfer_times:%,transfer_quantity:%,it_mix->received:%\n",quantity,account,it_mix->transfer_times,transfer_quantity,it_mix->received);
    print_f("id:%\n",id);
#endif
    m_mix_das.modify(it_mix, eosio::name(0), [&](auto& das){
        das.transfer_times -= 1;
        das.received =  asset(das.received.amount + transfer_quantity.amount, transfer_quantity.symbol);
    });

    if (it_mix->transfer_times == 0){
        m_mix_das.modify(it_mix, eosio::name(0), [&](auto& das){
            das.end_time =  now();
        });    

        auto itr_path = m_path_das.find(path->id);
        if (it_mix->account != _self){

            inline_transfer(_self, it_mix->account, it_mix->received,std::string("mix swap"),get_first_receiver());
            if (it_mix->fee.amount > 0 && quantity.symbol ==  EOS_SYMBOL){
                inline_transfer(_self, CHARGE_ACCOUNT, it_mix->fee,std::string("aggregator back fee"),get_first_receiver());
            }

            if (itr_path != m_path_das.end()){

                m_path_das.erase(itr_path);
            }
        }else{

            check(path != path_table.end(), "multi path is empty");
            path_index += 1;

            m_path_das.modify(itr_path, get_self(), [&](auto& row){

                row.current_path = path_index;
            });

            auto receiver = get_self();
            if (path_index == itr_path->total_path - 1){
                receiver = itr_path->account;
            }

            std::vector<std::string> multi_path;
            SplitString(itr_path->memo, multi_path, "|");

            check(path_index < multi_path.size(), "wrong path index");
            exchange(receiver, it_mix->received, multi_path[path_index]);
        }

        auto it_check =  m_check_das.find(id);
        if (it_check != m_check_das.end()){
            m_check_das.erase(it_check);
        }
    }

    del_mix_table();
}

void swap_aggregator::del_mix_table(){
    std::vector<uint64_t> ids;
    uint32_t times = 0;
    for (auto it = m_mix_das.begin(); it != m_mix_das.end(); it++){
        if (now() - it->end_time < SECONDS_PER_DAY * 3){
            break;
        }

        if (times > 0){
            break;
        }

        if (it->transfer_times != 0){
            continue;
        }

        ids.push_back(it->id);
        times++;
    } 

    for(auto id: ids){
        auto it = m_mix_das.find(id);
        m_mix_das.erase(it);
    }
}

uint64_t swap_aggregator::get_mix_id(uint32_t index){
    auto txHash = tx_hash();
    uint32_t current_index = 0;
    for (auto it = m_mix_das.rbegin(); it != m_mix_das.rend(); it++){
        if (it->trx_id == txHash && index == current_index){
            return it->id;
        }

        current_index ++;
    } 

    check(false, "get_mix_id can't find mix table id");
    return 0;
}

uint32_t swap_aggregator::now()
{
    return current_time_point().sec_since_epoch();
}


template <typename Das>
uint64_t swap_aggregator::find_id(Das &my_das, uint64_t pre_id)
{
    auto it_exchange = my_das.find(pre_id);
    if (it_exchange == my_das.end())
    {
        return pre_id;
    }

    return find_id(my_das, pre_id + 1);
}


void swap_aggregator::inline_transfer(const name &user, const name &to, const asset &quantity, const std::string &memo, const name &contract)
{
#ifdef DEBUG
    print_f("user:%,to:%,quantity:%,memo:%,contract:%\n",user,to,quantity,memo,contract);
#endif
    action(
        permission_level{user, ACTIVE},
        contract, TRANSFER,
        std::make_tuple(user, to, quantity, memo))
        .send();
}

checksum256 swap_aggregator::tx_hash()
{
    auto tx_size = eosio::transaction_size();
    char buff[tx_size];
    auto read = eosio::read_transaction(buff, tx_size);
    checksum256 hash = sha256(buff, read);

    return hash;
}

void swap_aggregator::SplitString(const std::string &in, std::vector<std::string> &out, const std::string &patterm)
{
    std::string::size_type first, end;
    end = in.find(patterm);
    first = 0;
    while (std::string::npos != end)
    {
        out.push_back(in.substr(first, end - first));
        first = end + patterm.size();
        end = in.find(patterm, first);
    }

    if (first != in.length())
        out.push_back(in.substr(first));
}

void swap_aggregator::delmix(uint32_t time){
    deltables(_self, m_mix_das, time);
}

template <typename TableDas>
void swap_aggregator::deltables(const name &pair,  TableDas &my_das, uint32_t time)
{
    set_local_operation();
    auto time_interval = time;
    std::vector<uint64_t> ids;
    for (auto &it_table : my_das)
    {
        if (now() - time_interval < it_table.end_time)
        {
            break;
        }

        ids.push_back(it_table.id);
    }

    for (auto id : ids)
    {
        auto it_table = my_das.find(id);
        my_das.erase(it_table);
    }
}


void swap_aggregator::setparams(const name &scope,std::vector<std::string> params){
    set_local_operation();
    parameter_das my_parameter_das(_self, scope.value);
    for(auto param : params){
        std::vector<std::string> patterms;
        SplitString(param, patterms, "-");
        check(patterms.size() == 2, "params is error, must equal two");
        trim(patterms[0]);
        trim(patterms[1]);
        inline_set_parameter(my_parameter_das, name{patterms[0]}, patterms[1]);
    }
}


template <class Das>
void swap_aggregator::inline_set_parameter(Das &das, const name &key, const std::string &value){
    auto it_parameter = das.find(key.value);
    if (it_parameter == das.end())
    {
        das.emplace(_self, [&](auto &das) {
            das.key = key;
            das.value = value;
        });
    }
    else
    {
        das.modify(it_parameter, eosio::name(0), [&](auto &das) {
            das.value = value;
        });
    }
}

void swap_aggregator::delparameter(const name &key, const name &scope)
{
    set_local_operation();
    parameter_das my_parameter_das(_self, scope.value);
    auto it_parameter = my_parameter_das.find(key.value);
    check(it_parameter != my_parameter_das.end(), "delparameter can't find pair");
    my_parameter_das.erase(it_parameter);
}

void swap_aggregator::fix(const asset &quantity, const name &contract){
    set_local_operation();
    inline_transfer(_self, PZAMOONFUNDS, quantity,std::string("fund"),contract);
}