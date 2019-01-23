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

   // map
   struct map {
      name         name;
      account_name account;
      bytes        data;

      EOSLIB_SERIALIZE( map, (name)(account)(data) )
   };

   // channel
   struct channel {
      name        chain;
      checksum256 id;

      vector<map> maps;

      EOSLIB_SERIALIZE( channel, (chain)(id)(maps) )
   };

private:

public:
   /// @abi action
   void commit( const name chain, const account_name transfer, const block_type& block ) {
      print( "commit ", chain );
   }

   /// @abi action
   void confirm( const name chain,
                 const account_name checker,
                 const checksum256 id,
                 const checksum256 mroot ) {
      print( "confirm ", chain );
   }

   /// @abi action
   void newchannel( const name chain,
                    const checksum256 id ) {
      print( "newchannel ", chain );
   }

   /// @abi action
   void newmap( const name chain,
                const name type,
                const account_name account ) {
      print( "newchannel ", chain );
   }
};


};




#endif //FORCEIO_CONTRACTS_FORCE_RELAY_HPP

