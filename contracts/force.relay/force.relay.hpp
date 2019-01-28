#ifndef FORCEIO_CONTRACTS_FORCE_RELAY_HPP
#define FORCEIO_CONTRACTS_FORCE_RELAY_HPP

#pragma once

#include <eosiolib/types.h>
#include <eosiolib/dispatcher.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/contract.hpp>
#include <eosiolib/multi_index.hpp>
#include <eosiolib/producer_schedule.hpp>
#include <eosiolib/time.hpp>
#include <eosiolib/chain.h>
#include <eosiolib/contract_config.hpp>
#include <vector>

using namespace eosio;

namespace force {

class relay : public eosio::contract {
public:
   explicit relay( account_name self )
      : contract(self)
      {}


public:
   // action
   struct action {
      account_name               account;
      action_name                name;
      vector<permission_level>   authorization;
      bytes                      data;

      EOSLIB_SERIALIZE( action, (account)(name)(authorization)(data) )
   };

   // block
   struct block_type {
   public:
      account_name            producer;
      checksum256             id;
      checksum256             previous;
      uint16_t                confirmed = 1;
      checksum256             transaction_mroot;
      checksum256             action_mroot;
      checksum256             mroot;

      vector<action>          actions;

      EOSLIB_SERIALIZE( block_type,
            (producer)(id)(previous)(confirmed)
            (transaction_mroot)(action_mroot)(action_mroot)(mroot)
            (actions) )
   };

   // map_handler
   struct map_handler {
      name         name;
      account_name action_account;
      action_name  action_name;

      account_name contract_account;
      bytes        data;

      EOSLIB_SERIALIZE( map_handler, (name)(action_account)(action_name)(contract_account)(data) )
   };

   // channel
   struct channel {
      name        chain;
      checksum256 id;

      vector<map_handler> handers;

      EOSLIB_SERIALIZE( channel, (chain)(id)(handers) )
   };

   typedef eosio::multi_index<N(channels), channel> channels_table;

private:

public:
   /// @abi action
   void commit( const name chain, const account_name transfer, const block_type& block );
   /// @abi action
   void confirm( const name chain,
                 const account_name checker,
                 const checksum256 id,
                 const checksum256 mroot );
   /// @abi action
   void newchannel( const name chain, const checksum256 id );
   /// @abi action
   void newmap( const name chain, const name type,
                const account_name act_account, const action_name act_name,
                const account_name account );
};


};




#endif //FORCEIO_CONTRACTS_FORCE_RELAY_HPP

