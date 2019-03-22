#include "force.system.hpp"
#include <relay.token/relay.token.hpp>

namespace eosiosystem {

    void system_contract::onblock( const block_timestamp, const account_name bpname, const uint16_t, const block_id_type,
                                  const checksum256, const checksum256, const uint32_t schedule_version ) {
      bps_table bps_tbl(_self, _self);
      schedules_table schs_tbl(_self, _self);

      account_name block_producers[NUM_OF_TOP_BPS] = {};
      get_active_producers(block_producers, sizeof(account_name) * NUM_OF_TOP_BPS);
      auto sch = schs_tbl.find(uint64_t(schedule_version));
      if( sch == schs_tbl.end()) {
         //换届
         schs_tbl.emplace(bpname, [&]( schedule_info& s ) {
            s.version = schedule_version;
            s.block_height = current_block_num();           //这个地方有一个清零的过程 不管
            for( int i = 0; i < NUM_OF_TOP_BPS; i++ ) {
               s.producers[i].amount = block_producers[i] == bpname ? 1 : 0;
               s.producers[i].bpname = block_producers[i];
            }
         });
         reset_block_weight(block_producers);
      } else {
         schs_tbl.modify(sch, 0, [&]( schedule_info& s ) {
            for( int i = 0; i < NUM_OF_TOP_BPS; i++ ) {
               if( s.producers[i].bpname == bpname ) {
                  s.producers[i].amount += 1;
                  break;
               }
            }
         });
      }

      INLINE_ACTION_SENDER(eosio::token, issue)( config::token_account_name, {{::config::system_account_name,N(active)}},
                                                 { ::config::system_account_name, 
                                                   asset(BLOCK_REWARDS_BP), 
                                                   "issue tokens for producer pay"} );

      print("UPDATE_CYCLE","---",current_block_num(),"---",UPDATE_CYCLE);
      if( current_block_num() % UPDATE_CYCLE == 0 ) {
         print("reward_bps\n");
         uint64_t  vote_power = get_vote_power();
         uint64_t  coin_power = get_coin_power();
         uint64_t total_power = vote_power + coin_power;
         print("before reward_block ",vote_power,"---",coin_power,"\n");
         reward_block(schedule_version);
         //reward miners
         reward_mines(BLOCK_REWARDS_POWER * coin_power / total_power);
         reward_bps(BLOCK_REWARDS_POWER * vote_power / total_power);
         //update schedule
         update_elected_bps();
      }
   }

   void system_contract::updatebp( const account_name bpname, const public_key block_signing_key,
                                   const uint32_t commission_rate, const std::string& url ) {
      require_auth(bpname);
      eosio_assert(url.size() < 64, "url too long");
      eosio_assert(1 <= commission_rate && commission_rate <= 10000, "need 1 <= commission rate <= 10000");

      bps_table bps_tbl(_self, _self);
      auto bp = bps_tbl.find(bpname);
      if( bp == bps_tbl.end()) {
         bps_tbl.emplace(bpname, [&]( bp_info& b ) {
            b.name = bpname;
            b.update(block_signing_key, commission_rate, url);
         });
      } else {
         bps_tbl.modify(bp, 0, [&]( bp_info& b ) {
            b.update(block_signing_key, commission_rate, url);
         });
      }
   }

   void system_contract::removebp( account_name bpname ) {
      require_auth(_self);

      bps_table bps_tbl(_self, _self);
      auto bp = bps_tbl.find(bpname);
      if( bp == bps_tbl.end()) {
        eosio_assert(false,"bpname is not registered");
      } else {
         bps_tbl.modify(bp, 0, [&]( bp_info& b ) {
            b.deactivate();
         });
      }
   }

   void system_contract::update_elected_bps() {
      bps_table bps_tbl(_self, _self);

      std::vector<eosio::producer_key> vote_schedule;
      std::vector<int64_t> sorts(NUM_OF_TOP_BPS, 0);

      for( auto it = bps_tbl.cbegin(); it != bps_tbl.cend(); ++it ) {
         for( int i = 0; i < NUM_OF_TOP_BPS; ++i ) {
            if( sorts[size_t(i)] <= it->total_staked && it->isactive) {
               eosio::producer_key key;
               key.producer_name = it->name;
               key.block_signing_key = it->block_signing_key;
               vote_schedule.insert(vote_schedule.begin() + i, key);
               sorts.insert(sorts.begin() + i, it->total_staked);
               break;
            }
         }
      }

      if( vote_schedule.size() > NUM_OF_TOP_BPS ) {
         vote_schedule.resize(NUM_OF_TOP_BPS);
      }

      /// sort by producer name
      std::sort(vote_schedule.begin(), vote_schedule.end());
      bytes packed_schedule = pack(vote_schedule);
      set_proposed_producers(packed_schedule.data(), packed_schedule.size());
   }

   void system_contract::reward_mines(const uint64_t reward_amount) {
      eosio::action(
         vector<eosio::permission_level>{{N(relay.token),N(active)}},
         N(relay.token),
         N(rewardmine),
         std::make_tuple(
            asset(reward_amount)
         )
      ).send();
   }

   // TODO it need change if no bonus to accounts

   void system_contract::reward_block(const uint32_t schedule_version ) {
      schedules_table schs_tbl(_self, _self);
      bps_table bps_tbl(_self, _self);
      print("reward_block",schedule_version,"\n");
      auto sch = schs_tbl.find(uint64_t(schedule_version));
      eosio_assert(sch != schs_tbl.end(),"cannot find schedule");
      int64_t total_block_out_age = 0;
      for( int i = 0; i < NUM_OF_TOP_BPS; i++ ) {
         auto bp = bps_tbl.find(sch->producers[i].bpname);
         eosio_assert(bp != bps_tbl.end(),"cannot find bpinfo");
         //eosio_assert(bp->mortgage > asset(88888888),"");
         if(bp->mortgage < asset(MORTGAGE)) {
            //抵押不足,没有出块奖励
            print("missing mortgage ",bp->name,"\n");
            continue;
         }
         bps_tbl.modify(bp, 0, [&]( bp_info& b ) {
            //先检测是否有漏块情况
            if(sch->producers[i].amount - b.last_block_amount != PER_CYCLE_AMOUNT && sch->producers[i].amount > b.last_block_amount) {
               b.block_age +=  (sch->producers[i].amount - b.last_block_amount) * b.block_weight;
               total_block_out_age += (sch->producers[i].amount - b.last_block_amount) * b.block_weight;
               b.block_weight = MORTGAGE;
               b.mortgage -= asset(0.2*10000);
               print("some block missing ",b.name,"\n");
            } else {
               b.block_age +=  (sch->producers[i].amount > b.last_block_amount ? sch->producers[i].amount - b.last_block_amount : sch->producers[i].amount) * b.block_weight;
               total_block_out_age +=  (sch->producers[i].amount > b.last_block_amount ? sch->producers[i].amount - b.last_block_amount : sch->producers[i].amount) * b.block_weight;
               b.block_weight += 1;
               print("normal reward block ",b.name,"\n");
            }
            b.last_block_amount = sch->producers[i].amount;
         });
      }
      reward_table reward_inf(_self,_self);
      auto reward = reward_inf.find(REWARD_ID);
      if(reward == reward_inf.end()) {
         reward_inf.emplace(_self, [&]( reward_info& s ) {
            s.id = REWARD_ID;
            s.reward_block_out = asset(BLOCK_REWARDS_BLOCK);
            s.reward_bp = asset(0);
            s.reward_develop = asset(0);
            s.total_block_out_age = total_block_out_age;
            s.total_bp_age = 0;
         });
      }
      else {
         reward_inf.modify(reward, 0, [&]( reward_info& s ) {
            s.reward_block_out += asset(BLOCK_REWARDS_BLOCK);
            s.total_block_out_age += total_block_out_age;
         });
      }
   }

   void system_contract::reward_bps(const uint64_t reward_amount) {
      bps_table bps_tbl(_self, _self);
      int64_t staked_all_bps = 0;
      for( auto it = bps_tbl.cbegin(); it != bps_tbl.cend(); ++it ) {
         staked_all_bps += it->total_staked;
      }
      if( staked_all_bps <= 0 ) {
         return;
      }
      const auto rewarding_bp_staked_threshold = staked_all_bps / 200;
      int64_t total_bp_age = 0;
      for( auto it = bps_tbl.cbegin(); it != bps_tbl.cend(); ++it ) {
         if( !it->isactive || it->total_staked <= rewarding_bp_staked_threshold || it->commission_rate < 1 ||
             it->commission_rate > 10000 ) {
            continue;
         }
         auto vote_reward = static_cast<int64_t>( reward_amount  * double(it->total_staked) / double(staked_all_bps));
         //暂时先给所有的BP都增加BP奖励   还需要增加vote池的奖励
         const auto& bp = bps_tbl.get(it->name, "bpname is not registered");
         bps_tbl.modify(bp, 0, [&]( bp_info& b ) {
            b.bp_age += 1;
            b.rewards_pool += asset(vote_reward);
         });
         total_bp_age +=1;
      }

      reward_table reward_inf(_self,_self);
      auto reward = reward_inf.find(REWARD_ID);
      if(reward == reward_inf.end()) {
         reward_inf.emplace(_self, [&]( reward_info& s ) {
            s.id = REWARD_ID;
            s.reward_block_out = asset(0);
            s.reward_bp = asset(BLOCK_REWARDS_BP);
            s.reward_develop = asset(0);
            s.total_block_out_age = 0;
            s.total_bp_age = total_bp_age;
         });
      }
      else {
         reward_inf.modify(reward, 0, [&]( reward_info& s ) {
            s.reward_bp += asset(BLOCK_REWARDS_BP);
            s.total_bp_age += total_bp_age;
         });
      }
   }

   void system_contract::reset_block_weight(account_name block_producers[]) {
      bps_table bps_tbl(_self, _self);
      for( int i = 0; i < NUM_OF_TOP_BPS; i++ ) {
         const auto& bp = bps_tbl.get(block_producers[i], "bpname is not registered");
         bps_tbl.modify(bp, 0, [&]( bp_info& b ) {
            b.block_weight = BLOCK_OUT_WEIGHT;
         });
      }
   }

   int64_t system_contract::get_coin_power() {
      int64_t total_power = 0; 
      rewards coin_reward(N(relay.token),N(relay.token));
      for( auto it = coin_reward.cbegin(); it != coin_reward.cend(); ++it ) {
         //根据it->chain  和it->supply 获取算力值     暂订算力值为supply.amount的0.1
         stats statstable(N(relay.token), it->chain);
         auto existing = statstable.find(it->supply.symbol.name());
         eosio_assert(existing != statstable.end(), "token with symbol already exists");
         total_power += existing->supply.amount * 0.1;
      }
      return total_power * 10000;
   }

   int64_t system_contract::get_vote_power() {
      bps_table bps_tbl(_self, _self);
      int64_t staked_all_bps = 0;
      for( auto it = bps_tbl.cbegin(); it != bps_tbl.cend(); ++it ) {
         staked_all_bps += it->total_staked;
      }
      return staked_all_bps;
   }
   //增加抵押
   void system_contract::addmortgage(const account_name bpname,const account_name payer,asset quantity) {
      require_auth(payer);
      bps_table bps_tbl(_self, _self);
      auto bp = bps_tbl.find(bpname);
      eosio_assert(bp != bps_tbl.end(),"can not find the bp");
      bps_tbl.modify(bp, 0, [&]( bp_info& b ) {
            b.mortgage += quantity;
         });

      INLINE_ACTION_SENDER(eosio::token, transfer)(
               config::token_account_name,
               { payer, N(active) },
               { payer, ::config::system_account_name, asset(quantity), "add mortgage" });
   }
   //提取抵押
   void system_contract::claimmortgage(const account_name bpname,const account_name receiver,asset quantity) {
      require_auth(bpname);
      bps_table bps_tbl(_self, _self);
      auto bp = bps_tbl.find(bpname);
      eosio_assert(bp != bps_tbl.end(),"can not find the bp");
      bps_tbl.modify(bp, 0, [&]( bp_info& b ) {
            b.mortgage -= quantity;
         });
      
      INLINE_ACTION_SENDER(eosio::token, transfer)(
         config::token_account_name,
         { ::config::system_account_name, N(active) },
         { ::config::system_account_name, receiver, quantity, "claim bp mortgage" });
   }

   bool system_contract::is_super_bp( account_name block_producers[], account_name name ) {
      for( int i = 0; i < NUM_OF_TOP_BPS; i++ ) {
         if( name == block_producers[i] ) {
            return true;
         }
      }
      return false;
   }
    
}
