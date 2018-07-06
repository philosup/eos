#include <eosiolib/eosio.hpp>
#include <eosiolib/asset.hpp>
#include <string>

using namespace eosio;
namespace sevenchain{
	using std::string;
	class token : public contract
	{
		private:
			struct account{
				asset	balance;
				bool	frozen = false;
				bool  	whitelist = true;
				uint64_t primary_key() const {return balance.symbol.name();}
			};
			struct currency_stats{
				asset	supply;
				asset	max_supply;
				account_name issuer;
				bool can_freeze =true;
				bool can_whitelist = true;
				bool is_frozen =false;
				bool enforce_whitelist = false;

				uint64_t primary_key() const {return supply.symbol.name();}
			};
			typedef eosio::multi_index<N(accounts), account> accounts;
			typedef eosio::multi_index<N(stat), currency_stats> stats;

			void sub_balance(account_name owner, asset value, const currency_stats& st)
			{
				accounts from_acnts( _self, owner );

				const auto& from = from_acnts.get( value.symbol.name() );
				eosio_assert( from.balance.amount >= value.amount, "overdrawn balance" );

				if( has_auth( owner ) ) {
					eosio_assert( !st.can_freeze || !from.frozen, "account is frozen by issuer" );
					eosio_assert( !st.can_freeze || !st.is_frozen, "all transfers are frozen by issuer" );
					eosio_assert( !st.enforce_whitelist || from.whitelist, "account is not white listed" );
				} else if( has_auth( st.issuer ) ) {
					eosio_assert( st.can_recall, "issuer may not recall token" );
				} else {
					eosio_assert( false, "insufficient authority" );
				}

				from_acnts.modify( from, owner, [&]( auto& a ) {
						a.balance -= value;
						});
			}
			void add_balance(account_name owner, asset value, const currency_stats& st, account_name ram_payer)
			{
				accounts to_acnts( _self, owner );
				auto to = to_acnts.find( value.symbol.name() );
				if( to == to_acnts.end() ) {
					eosio_assert( !st.enforce_whitelist, "can only transfer to white listed accounts" );
					to_acnts.emplace( ram_payer, [&]( auto& a ){
							a.balance = value;
							});
				} else {
					eosio_assert( !st.enforce_whitelist || to->whitelist, "receiver requires whitelist by issuer" );
					to_acnts.modify( to, 0, [&]( auto& a ) {
							a.balance += value;
							});
				}
			}
		public:
			token(account_name self):contract(self){}

			void create(account_name issuer, asset maximum_supply, uint8_t issuer_can_freeze=false, uint8_t issuer_can_whitelist=false)
			{
				require_auth(_self);

				auto sym = maximum_supply.symbol;
				eosio_assert(sym.is_valid(), "invalid symbol name");
				eosio_assert(maximum_supply.is_valid(), "invalid supply");
				eosio_assert(maximum_supply.amount > 0, "max-supply must be positive");

				stats statstable(_self, sym.name());
				auto existing = statstable.find(sym.name());
				eosio_assert(existing == statstable.end(), "token with symbol already exists");

				statstable.emplace(_self, [&](auto& s){
						s.supply.symbol	= maximum_supply.symbol;
						s.max_supply		= maximum_supply;
						s.issuer				= issuer;
						s.can_freeze		= issuer_can_freeze;
						s.can_whitelist	= issuer_can_whitelist;
						});
			}
			void issue(account_name to, asset quantity, string memo)
			{
				print( "issue" );
				auto sym = quantity.symbol;
            eosio_assert( sym.is_valid(), "invalid symbol name" );
            eosio_assert( memo.size() <= 256, "memo has more than 256 bytes");

            auto sym_name = sym.name();
				stats statstable( _self, sym_name );
            auto existing = statstable.find( sym_name );
            eosio_assert( existing != statstable.end(), "token with symbol does not exist, create token before issue" );

				const auto& st = *existing;

				require_auth( st.issuer );
				eosio_assert( quantity.is_valid(), "invalid quantity" );
				eosio_assert( quantity.amount > 0, "must issue positive quantity" );

            eosio_assert( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
				eosio_assert( quantity.amout <= st.max_supply.amount - st.supply.amount, "quantity exceeds available supply");

				statstable.modify( st, 0, [&]( auto& s ) {
						s.supply += quantity;
						});

				add_balance( st.issuer, quantity, st, st.issuer );

				if( to != st.issuer )
				{
               SEND_INLINE_ACTION( *this, transfer, {st.issuer, N(active))}, {st.issuer, to, quantity, memo} );
					//dispatch_inline( permission_level{st.issuer,N(active)}, _self, N(transfer), &token::transfer, { st.issuer, to, quantity, memo } );
				}
			}
			void inlineissue(asset quantity, string memo)
			{
				auto sym = quantity.symbol.name();
				stats statstable( _self, sym );
				const auto& st = statstable.get( sym );

				require_auth( st.issuer );
				eosio_assert( quantity.is_valid(), "invalid quantity" );
				eosio_assert( quantity.amount > 0, "must issue positive quantity" );

				dispatch_inline( permission_level{st.issuer,N(active)}, _self, N(issue), &token::issue, { st.issuer, quantity, memo } );
			}

         void inline_transfer(account_name from, account_name to, asset quantity, string memo)
         {
            require_auth(from);

            require_recipient(from);
            require_recipient(to);

            dispatch_inline(permission_level{from,N(active)}, _self, N(transfer), &token::transfer, {from, to, quantity, memo});
         }

			void transfer(account_name from, account_name to, asset quantity, string memo)
			{
            eosio_assert( from != to, "cannot transfer to self");
				require_auth( from );
            eosio_assert( is_account( to), "to account does not exist");
				auto sym = quantity.symbol.name();
				stats statstable( _self, sym );
				const auto& st = statstable.get( sym );

				require_recipient( from );
				require_recipient( to );

				eosio_assert( quantity.is_valid(), "invalid quantity" );
				eosio_assert( quantity.amount > 0, "must transfer positive quantity" );
            eosio_assert( quantity.symbol == st.supply.symbol. "symbol precision mismatch");
            eosio_assert( memo.size() <= 256, "memo has more than 256 bytes");

				sub_balance( from, quantity, st );
				add_balance( to, quantity, st, from );
			}
			asset get_total_supply(const symbol_type& symbol)
			{
				auto symbol_name = symbol.name();
				stats statstable( _self, symbol_name );
				const auto& st = statstable.get( symbol_name );
				return st.supply;
			}
	};
}
