_Pragma("once")
#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>


using namespace eosio;
class[[eosio::contract]] swap_aggregator : public eosio::contract
{
private:
    struct newpool{
        uint64_t first_amount;
        uint64_t second_amount;
    }; 

    std::string string_join(std::vector<std::string> &str, std::string split);
    void SplitString(const std::string &in, std::vector<std::string> &out, const std::string &patterm);
    void trim(std::string &s);

    double get_price(std::string molecule, std::string denominator);
    double get_split_price(std::string desc);
    //获取手续费率
    double get_fee_rate(const asset &quantity);
    //获取权重
    double get_weight_rate(uint32_t weight);

    void verify();
    void set_local_operation();

    void del_mix_table();
    template <typename TableDas>
    void deltables(const name &pair,  TableDas &my_das, uint32_t time);
    template <class Das>
    void inline_set_parameter(Das &das, const name &key, const std::string &value);

    template <typename Das>
    uint64_t find_id(Das &my_das, uint64_t pre_id);
    checksum256 tx_hash();
    void inline_transfer(const name &user, const name &to, const asset &quantity, const std::string &memo, const name &contract);
    uint32_t now();

    uint64_t get_mix_id(uint32_t index);

    void new_calculate_dk(uint64_t first, uint64_t second, const asset &p, double wave, const name &channel);   

    void exchange(const name &account, const asset &quantity, std::string &memo);

    auto getnewpool(uint64_t box_id, uint64_t dfs_id, uint64_t dop_id, uint64_t rome_id,const std::string &pizza_memo,const asset &quantity, std::map<name,newpool> &r);

    auto new_distribute(const std::map<name,newpool> & pools, uint64_t first_total_amount, uint64_t second_total_amount, const asset &quantity);

    void handle_mix(const name &account, const asset &quantity);
public:
    swap_aggregator(name receiver, name code, datastream<const char *> ds);
    void transfer(name from, name to, asset quantity, std::string memo);
    [[eosio::action]] void delmix(uint32_t time);
    [[eosio::action]] void setparams(const name &scope,std::vector<std::string> params);
    [[eosio::action]]  void delparameter(const name &key, const name &scope);

    [[eosio::action]] void pause(uint32_t status);
    [[eosio::action]] void fix(const asset &quantity, const name &contract);


    struct [[eosio::table]] mix_data{
        uint64_t id;
        name account;
        checksum256 trx_id;
        asset send;
        asset fee;
        asset received;
        uint32_t transfer_times;
        uint32_t start_time;
        uint32_t end_time;
        uint64_t primary_key() const { return id; }       
    };


    struct [[eosio::table]] pause_data
    {
        uint64_t id;
        uint32_t status;

        uint64_t primary_key() const { return 0; }
    };


     struct [[eosio::table]] parameter_data
    {
        name key;
        std::string value;

        uint64_t primary_key() const { return key.value; }
    };


    struct [[eosio::table]] check_data
    {
        uint64_t id;
        name contract;

        uint64_t primary_key() const { return id; }
    };

    struct [[eosio::table]] multi_path
    {
        uint64_t id;
        checksum256 trx_id;
        name account;
        std::string memo;
        uint32_t total_path;
        uint32_t current_path;

        uint64_t primary_key() const { return id; }
        checksum256 get_secondary() const {

            return trx_id;
        }
    };

    typedef eosio::multi_index<"mix"_n, mix_data> mix_das;
    mix_das m_mix_das;

    typedef eosio::multi_index<"parameter"_n, parameter_data> parameter_das;

    typedef eosio::multi_index<"pause"_n, pause_data> pause_das;
    pause_das m_pause_das;

    typedef eosio::multi_index<"check"_n, check_data> check_das;
    check_das m_check_das;

    typedef eosio::multi_index<"path"_n, multi_path, indexed_by<name("trxid"), const_mem_fun<multi_path, checksum256, &multi_path::get_secondary>>> multi_paths;
    multi_paths m_path_das;
};