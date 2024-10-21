/** $lic$
 * Copyright (C) 2012-2015 by Massachusetts Institute of Technology
 * Copyright (C) 2010-2013 by The Board of Trustees of Stanford University
 *
 * This file is part of zsim.
 *
 * zsim is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, version 2.
 *
 * If you use this software in your research, we request that you reference
 * the zsim paper ("ZSim: Fast and Accurate Microarchitectural Simulation of
 * Thousand-Core Systems", Sanchez and Kozyrakis, ISCA-40, June 2013) as the
 * source of the simulator in any publications that use this software, and that
 * you send us a citation of your work.
 *
 * zsim is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef FILTER_CACHE_H_
#define FILTER_CACHE_H_

#include "bithacks.h"
#include "cache.h"
#include "galloc.h"
#include "zsim.h"
#include "coherence_ctrls.h"
/* Extends Cache with an L0 direct-mapped cache, optimized to hell for hits
 *
 * L1 lookups are dominated by several kinds of overhead (grab the cache locks,
 * several virtual functions for the replacement policy, etc.). This
 * specialization of Cache solves these issues by having a filter array that
 * holds the most recently used line in each set. Accesses check the filter array,
 * and then go through the normal access path. Because there is one line per set,
 * it is fine to do this without grabbing a lock.
 */

class FilterCache : public Cache {
    private:
        struct FilterEntry {
            volatile Address rdAddr;
            volatile Address wrAddr;
            volatile uint64_t availCycle;

            void clear() {wrAddr = 0; rdAddr = 0; availCycle = 0;}
        };

        //Replicates the most accessed line of each set in the cache
        FilterEntry* filterArray;
        Address setMask;
        uint32_t numSets;
        uint32_t srcId; //should match the core
        uint32_t reqFlags;

        lock_t filterLock;
        uint64_t fGETSHit, fGETXHit;
    public:

		uint64_t cache_load,cache_store;
        FilterCache(uint32_t _numSets, uint32_t _numLines, CC* _cc, CacheArray* _array,
                ReplPolicy* _rp, uint32_t _accLat, uint32_t _invLat, g_string& _name)
            : Cache(_numLines, _cc, _array, _rp, _accLat, _invLat, _name)
        {
            numSets = _numSets;
            setMask = numSets - 1;
            filterArray = gm_memalign<FilterEntry>(CACHE_LINE_BYTES, numSets);
            for (uint32_t i = 0; i < numSets; i++) filterArray[i].clear();
            futex_init(&filterLock);
            fGETSHit = fGETXHit = 0;
            srcId = -1;
            reqFlags = 0;
			cache_load = cache_store = 0;
        }

        void setSourceId(uint32_t id) {
			//std::cout<<"change source id to:"<<id<<std::endl;
            srcId = id;
        }

        void setFlags(uint32_t flags) {
            reqFlags = flags;
        }

        void initStats(AggregateStat* parentStat) {
            AggregateStat* cacheStat = new AggregateStat();
            cacheStat->init(name.c_str(), "Filter cache stats");

            ProxyStat* fgetsStat = new ProxyStat();
            fgetsStat->init("fhGETS", "Filtered GETS hits", &fGETSHit);
            ProxyStat* fgetxStat = new ProxyStat();
            fgetxStat->init("fhGETX", "Filtered GETX hits", &fGETXHit);
            cacheStat->append(fgetsStat);
            cacheStat->append(fgetxStat);

            initCacheStats(cacheStat);
            parentStat->append(cacheStat);
        }

        inline uint64_t load(Address vAddr, uint64_t curCycle) {
			cache_load++;
            Address vLineAddr = vAddr >> lineBits;
            uint32_t idx = vLineAddr & setMask;
			//std::cout<<"load( "<<std::hex<<vLineAddr<<" , "<<idx<<" ) ,"<<srcId<<std::endl;
            uint64_t availCycle = filterArray[idx].availCycle; //read before, careful with ordering to avoid timing races
            if (vLineAddr == filterArray[idx].rdAddr) {
                fGETSHit++;
                return MAX(curCycle, availCycle);
            } else {
			    //std::cout<<"load miss:0x"<<std::hex<<(vAddr>>12)<<" , "<<srcId<<" vLineAddr:"<<std::hex<<vLineAddr<<std::endl;
                return replace(vLineAddr, idx, true, curCycle);
            }
        }

        inline uint64_t store(Address vAddr, uint64_t curCycle) {
			cache_store++;
            Address vLineAddr = vAddr >> lineBits;
            uint32_t idx = vLineAddr & setMask;
			//std::cout<<"store( "<<std::hex<<vLineAddr<<" , "<<idx<<" ) src:"<<srcId<<std::endl;
            uint64_t availCycle = filterArray[idx].availCycle; //read before, careful with ordering to avoid timing races
            if (vLineAddr == filterArray[idx].wrAddr) {
                fGETXHit++;
                //NOTE: Stores don't modify availCycle; we'll catch matches in the core
                //filterArray[idx].availCycle = curCycle; //do optimistic store-load forwarding
                return MAX(curCycle, availCycle);
            } else {
			//std::cout<<"store miss:0x"<<std::hex<<(vAddr>>12)<<" , "<<"vLineAddr:"<<std::hex<<vLineAddr<<srcId<<std::endl;
				/*int32_t line_id = array->lookup(vLineAddr, NULL, false);
				if( line_id == -1)
				{
					std::cout<<"fetch again, curCYcle:"<<curCycle<<std::endl;
					 replace(vLineAddr, idx, true, curCycle);
					std::cout<<"fetch after, curCycle:"<<curCycle<<std::endl;
				}*/
                return replace(vLineAddr, idx, false, curCycle);
            }
        }

		inline uint64_t clflush( Address lineAddr, uint64_t curCycle, bool flush_all, bool &is_dirty)
		{
			uint64_t respCycle = curCycle;
			bool req_write_back = true;				
			is_dirty = false;
			uint32_t idx = lineAddr & setMask;
			if( !flush_all )
			{
				if( lineAddr == filterArray[idx].wrAddr)
				{
					is_dirty = true;
				}
				
				int32_t line_id = array->lookup(lineAddr, NULL, false);
				//if( lineAddr == filterArray[idx].rdAddr || lineAddr == filterArray[idx].wrAddr)
				if( line_id !=-1)
				{
					//std::cout<<"clflush"<<lineAddr<<std::endl;
					respCycle = clflush_all(lineAddr, INV, &req_write_back, respCycle, srcId);
					//std::cout<<"clflush latency:"<<std::dec<<(respCycle-curCycle)<<std::endl;
					//std::cout<<"###"<<std::endl;
				}
			}
			else if( lineAddr == filterArray[idx].rdAddr || lineAddr == filterArray[idx].wrAddr)
			{
				if( lineAddr == filterArray[idx].wrAddr)
					is_dirty = true;
				respCycle = clflush_all(lineAddr, INV, &req_write_back, curCycle, srcId);
				//std::cout<<"clflush latency:"<<std::dec<<(respCycle-curCycle)<<std::endl;
			}
			return respCycle;
		}


        uint64_t replace(Address vLineAddr, uint32_t idx, bool isLoad, uint64_t curCycle) 
		{
            futex_lock(&filterLock);
            //Address pLineAddr = procMask | vLineAddr;
            Address pLineAddr =  vLineAddr;
            MESIState dummyState = MESIState::I;
            MemReq req = {pLineAddr, isLoad? GETS : GETX, 0, &dummyState, curCycle, &filterLock, dummyState, srcId, reqFlags};
			//std::cout<<"miss:0x"<<std::hex<<(pLineAddr)<<" , "<<req.srcId<<" , "<<srcId<<std::endl;
            uint64_t respCycle  = access(req);

            //Due to the way we do the locking, at this point the old address might be invalidated, but we have the new address guaranteed until we release the lock

            //Careful with this order
            Address oldAddr = filterArray[idx].rdAddr;
            filterArray[idx].wrAddr = isLoad? -1L : vLineAddr;
            filterArray[idx].rdAddr = vLineAddr;
			//std::cout<<"install:"<<oldAddr<<"->"<<filterArray[idx].rdAddr<<" idx:"<<idx<<std::endl;
			//std::cout<<"set address after replace:"<<filterArray[idx].rdAddr;
            //For LSU simulation purposes, loads bypass stores even to the same line if there is no conflict,
            //(e.g., st to x, ld from x+8) and we implement store-load forwarding at the core.
            //So if this is a load, it always sets availCycle; if it is a store hit, it doesn't
            if (oldAddr != vLineAddr) filterArray[idx].availCycle = respCycle;

            futex_unlock(&filterLock);
            return respCycle;
        }


        //NOTE: reqWriteback is pulled up to true, but not pulled down to false.
        uint64_t invalidate(Address lineAddr, InvType type, bool* reqWriteback, uint64_t cycle, uint32_t srcId)	{
            futex_lock(&filterLock);
            uint32_t idx = lineAddr & setMask; //works because of how virtual<->physical is done...
            //if ((filterArray[idx].rdAddr | procMask) == lineAddr) { //FIXME: If another process calls invalidate(), procMask will not match even though we may be doing a capacity-induced invalidation!
            if ((filterArray[idx].rdAddr) == lineAddr) { //FIXME: If another process calls invalidate(), procMask will not match even though we may be doing a capacity-induced invalidation!
                filterArray[idx].wrAddr = -1L;
                filterArray[idx].rdAddr = -1L;
            }
            futex_unlock(&filterLock);
            uint64_t respCycle = Cache::invalidate(lineAddr, type, reqWriteback, cycle, srcId);
            return respCycle;
        }

        void contextSwitch() {
            futex_lock(&filterLock);
            for (uint32_t i = 0; i < numSets; i++) filterArray[i].clear();
            futex_unlock(&filterLock);
        }
		virtual void calculate_stats()
		{
			//info("%s cache load: %lu, store: %lu",name,cache_load , cache_load);
		}
};
#endif  // FILTER_CACHE_H_
