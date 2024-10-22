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

#include "cache.h"
#include "hash.h"

Cache::Cache(uint32_t _numLines, CC* _cc, CacheArray* _array, ReplPolicy* _rp, uint32_t _accLat, uint32_t _invLat, const g_string& _name)
    : cc(_cc), array(_array), rp(_rp), numLines(_numLines), accLat(_accLat), invLat(_invLat), name(_name) {}

const char* Cache::getName() {
    return name.c_str();
}

void Cache::setParents(uint32_t childId, const g_vector<MemObject*>& parents, Network* network) {
    cc->setParents(childId, parents, network);
}

void Cache::setChildren(const g_vector<BaseCache*>& children, Network* network) {
    cc->setChildren(children, network);
}

void Cache::initStats(AggregateStat* parentStat) {
    AggregateStat* cacheStat = new AggregateStat();
    cacheStat->init(name.c_str(), "Cache stats");
    initCacheStats(cacheStat);
    parentStat->append(cacheStat);
}

void Cache::initCacheStats(AggregateStat* cacheStat) {
    cc->initStats(cacheStat);
    array->initStats(cacheStat);
    rp->initStats(cacheStat);
}

uint64_t Cache::access(MemReq& req) {
    uint64_t respCycle = req.cycle;

    bool skipAccess = cc->startAccess(req); //may need to skip access due to races (NOTE: may change req.type!);
	if( skipAccess)
		std::cout<<"skip access"<<std::endl;
    if (likely(!skipAccess)) {
        bool updateReplacement = (req.type == GETS) || (req.type == GETX);
        int32_t lineId = array->lookup(req.lineAddr, &req, updateReplacement);
        respCycle += accLat;

        if (lineId == -1 && cc->shouldAllocate(req)) {
            //Make space for new line
            Address wbLineAddr;
            lineId = array->preinsert(req.lineAddr, &req, &wbLineAddr); //find the lineId to replace
            trace(Cache, "[%s] Evicting 0x%lx", name.c_str(), wbLineAddr);

            //Evictions are not in the critical path in any sane implementation -- we do not include their delays
            //NOTE: We might be "evicting" an invalid line for all we know. Coherence controllers will know what to do
            cc->processEviction(req, wbLineAddr, lineId, respCycle); //1. if needed, send invalidates/downgrades to lower level
            array->postinsert(req.lineAddr, &req, lineId); //do the actual insertion. NOTE: Now we must split insert into a 2-phase thing because cc unlocks us.
			//std::cout<<"install "<<req.lineAddr<<" id:"<<lineId<<std::endl;
        }
        respCycle = cc->processAccess(req, lineId, respCycle);
    }
    cc->endAccess(req);
	//std::cout<<"access**:"<<req.lineAddr<<" done"<<std::endl;
    assert_msg(respCycle >= req.cycle, "[%s] resp < req? 0x%lx type %s childState %s, respCycle %ld reqCycle %ld",
            name.c_str(), req.lineAddr, AccessTypeName(req.type), MESIStateName(*req.state), respCycle, req.cycle);
    return respCycle;
}

void Cache::startInvalidate() {
    cc->startInv(); //note we don't grab tcc; tcc serializes multiple up accesses, down accesses don't see it
}

uint64_t Cache::finishInvalidate(const InvReq& req) {
    int32_t lineId = array->lookup(req.lineAddr, nullptr, false);
    assert_msg(lineId != -1, "[%s] Invalidate on non-existing address 0x%lx type %s lineId %d, reqWriteback %d", name.c_str(), req.lineAddr, InvTypeName(req.type), lineId, *req.writeback);
    uint64_t respCycle = req.cycle + invLat;
    trace(Cache, "[%s] Invalidate start 0x%lx type %s lineId %d, reqWriteback %d", name.c_str(), req.lineAddr, InvTypeName(req.type), lineId, *req.writeback);
    respCycle = cc->processInv(req, lineId, respCycle); //send invalidates or downgrades to children, and adjust our own state
    trace(Cache, "[%s] Invalidate end 0x%lx type %s lineId %d, reqWriteback %d, latency %ld", name.c_str(), req.lineAddr, InvTypeName(req.type), lineId, *req.writeback, respCycle - req.cycle);

    return respCycle;
}

uint64_t Cache::clflush_all( Address lineAddr, InvType type, bool* reqWriteback, uint64_t reqCycle, uint32_t srcId)
{
	int32_t lineId = array->lookup(lineAddr, NULL, false);
	//std::cout<<"lineId:"<<lineId<<" addr:"<<lineAddr<<std::endl;
	uint64_t respCycle = reqCycle;
	if( -1 != lineId)
	{
		MemObject* obj= cc->getParent(lineAddr);
		MemObject* pre = obj;
		//std::cout<<"parent:"<<obj<<" of:"<<this<<" srcId:"<<srcId<<" addr:"<<lineAddr<<std::endl;
		//std::cout<<"invalidate parent:"<<obj<<" srcId:"<<srcId<<std::endl;
		while( dynamic_cast<BaseCache*>(obj) )
		{
			pre=obj;
			obj=dynamic_cast<Cache*>(obj)->cc->getParent(lineAddr);
			//std::cout<<"parent:"<<obj<<" of:"<<pre<<" srcId:"<<srcId<<" addr:"<<lineAddr<<std::endl;
		}
		if( pre )
		{
			//std::cout<<"invalidate inner:"<<lineAddr<<" srcId:"<<srcId<<" objaddr:"<<obj<<" reqCycle:"<<reqCycle<<" respCycle:"<<respCycle<<std::endl;
		 	dynamic_cast<Cache*>(pre)->cc->endInv(); //in case deadlock
		 	//dynamic_cast<Cache*>(pre)->cc->startInval(); //in case deadlock
			InvReq req = {lineAddr, type, reqWriteback, reqCycle, srcId};
			respCycle = dynamic_cast<Cache*>(pre)->invalidate(req);
		 	//dynamic_cast<Cache*>(pre)->cc->endInval2(); //in case deadlock	
		}
	}
	return respCycle;
}
