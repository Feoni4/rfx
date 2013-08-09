#include "rfx_api.h"
#include "rfx_modules.h"
#include "cconsole.h"
#include <string.h>
#include <stdio.h>
#include <map>

#define	LOOT_DROP_NEW		0x1403
#define	LOOT_DROP_HORIZON	0x0f04
#define	LOOT_DISAPPEAR		0x1c03

#define	ITEM_PICK			0x0207
#define PICK_RESPONSE		0x0407

#if 0
template<typename T>
struct refptr {
	T	*data;
	unsigned	ref;

	refptr() : data(NULL), ref(1) {}
	refptr(T *data) : data(NULL), ref(1) {}
	~refptr() { unref(); }

	refptr<T>* ref() { ref++; return this; }
	void unref() { if (ref && !(--ref)) { delete data; data = NULL; } }
};
#endif


/* ground drop set {{{ */
struct ground_item {
	unsigned		id;			/* hex id of the item */
	unsigned		ground_id;	/* identifier of the item layin on ground */
	rf_packet_t		*source;	/* packet that generated that item */

	ground_item() : id(0), ground_id(0), source(NULL) {}
	ground_item(unsigned id, unsigned ground_id, rf_packet_t *source) : id(id), ground_id(ground_id), source(source) {
		if (source)
			pkt_ref(source);
	}
	ground_item(const ground_item &gi) : source(NULL) { *this = gi; }
	~ground_item() { if (source) pkt_unref(source); }

	ground_item& operator=(const ground_item &gi) {
		id = gi.id;
		ground_id = gi.ground_id;
		if (gi.source)
			pkt_ref(gi.source);
		if (source)
			pkt_unref(source);
		source = gi.source;
		return *this;
	}
};

typedef std::map<unsigned, ground_item> ground_items;
/* }}} */

static rf_packet_t*
make_pick_request(unsigned ground_id, unsigned inventory_cell)
{
	rf_packet_t *pkt = pkt_new(8, ITEM_PICK, CLI_TO_SRV);
	uint8_t *p = pkt->data + 4;
	*p++ = ground_id & 0xff;
	*p++ = (ground_id >> 8) & 0xff;
	*p++ = inventory_cell & 0xff;
	*p++ = (inventory_cell >> 8) & 0xff;
	pkt->desc = "[INJECT] pick request";
	//pkt_dump(pkt);
	return pkt;
}

struct loot_state {
	loot_mask		show;
	loot_mask		pick;

	loot_state() : show(true), pick(false) {}
	loot_state(const loot_state &s) : show(s.show), pick(s.pick) {}

	bool save(rfx_state *s) { show.save(s); pick.save(s); return true; }
	bool restore(rfx_state *s) { return show.restore(s) && pick.restore(s); }
};

struct pick_request {
	/* gid & iid are mandatory */
	uint16_t		gid;		/* ground_id */
	uint16_t		iid;		/* inventory id */
	unsigned		code;		/* optional, not always known */
	pick_request() {}
	~pick_request() { release(); }
	void release() { /* if (rq) { pkt_unref(rq); rq = NULL; } */ }
	rf_packet_t *operator()(rfx_pick_do_event *e) {
		rf_packet_t *rq;
		gid = e->gid; iid = e->iid; code = e->code;
		rq = make_pick_request(gid, iid);
		rq->delay = e->delay;
		//printf("scheduled pick: 0x%hx => 0x%hx in %u ms\n", gid, iid, rq->delay);
		return rq;
	}
	rf_packet_t* operator()() {
		return make_pick_request(gid, iid);
	}
};

class rfx_loot : public rfx_filter {
	loot_state		lm;
	pick_request	pick_rq, *prq;
public:
	rfx_loot() : prq(NULL) {}

	int process(rf_packet_t *pkt, pqhead_t *pre, pqhead_t *post, evqhead_t *evq);
	int process(rfx_event *ev, pqhead_t *pre, pqhead_t *post, evqhead_t *evq);

	void save_state(rfx_state *s) { lm.save(s); }
	bool load_state(rfx_state *s) { return lm.restore(s);  }

	std::string loot_cmd(const std::string &msg, loot_mask &m);
};

static bool
dumb_test_good(int code)
{
	switch (code) {
	/* HP/FP 25 */
	case 0x25E08: //	iiaaa95
	case 0x25F08: //	iiaaa96
	case 0x26008: //	iiaaa97
	case 0x26108: //	iiaaa98
	case 0x26C08: //	iiaab10
	case 0x26D08: //	iiaab11
	case 0x26E08: //	iiaab12
	case 0x26F08: //	iiaab13
		return false;
	/* ATK-DODGE */
	case 0x29108: //	iiaab47	ring 20/15
	case 0x29208: //	iiaab48	ring 20/15
	case 0x29308: //	iiaab49	ring 20/15
	case 0x29408: //	iiaab50	ring 25/20
	case 0x29109: //	iaaab47	amulet 20/15
	case 0x29209: //	iaaab48	amulet 20/15
	case 0x29309: //	iaaab49	amulet 20/15
	case 0x29409: //	iaaab50	amulet 25/20
		return true;
	/* ATK-DEF */
	case 0x29908: //	iiaab55	ring 20/20
	case 0x29A08: //	iiaab56	ring 20/20
	case 0x29B08: //	iiaab57	ring 20/20
	case 0x29C08: //	iiaab58	ring 25/25
	case 0x29909: //	iaaab55	amulet 20/20
	case 0x29A09: //	iaaab56	amulet 20/20
	case 0x29B09: //	iaaab57	amulet 20/20
	case 0x29C09: //	iaaab58	amulet 25/25
		return true;
	/* DEF-DODGE */
	case 0x2AC08: //	iiaab74 ring 25/20
		return true;
	/* PB related items */
	case 0x2412: // t3 ruby
	case 0x2712: // t3 brilliant
	case 0x2a12: // t3 topaz	
	case 0x2d12: // t3 obsidian
	case 0x4412: // t5 ruby
	case 0x4512: // t5 brilliant
	case 0x4612: // t5 topaz	
	case 0x4712: // t5 obsidian
	case 0x1412: // ignore
	case 0x2012: // mercy	
	case 0x1200d: // rescue rune
	case 0x1210d: // defence rune
	case 0x2112: // restoration tallic
	case 0x1912: // favor tallic
		return true;
	/* 50 int weapons */
	case 0xC506:  //	iwknb50 one-handed sword
	case 0x18D06: //	iwswb50 two-handed sword
	case 0x4AD06: //	iwspb50 spear	
	/* 50 int weapons */
	case 0x213506: //	one-handed sword	
	case 0x213D06: //	spear
		return true;
	/* misc stuff */
	case 0x42509: //dagom amulet
	case 0x44008: //dagnu ring
	case 0x44108: //dagan ring
	case 0x25814: //perfect relic seal
	case 0x4F414: //perfect relic seal
	case 0x441F: //relic box
	case 0x451F: //relic box
	case 0x461F: //relic box
	case 0x174403: //	53 in laucher boots
	case 0x174303: // ?? test_items says this is actual boots identifier
		return true;
	/* lucky boxes */
	case 0xB71F: //small
	case 0xB81F: //mid
	case 0xB91F: //big
	case 0xBA1F: //relic box
	case 0xBB1F: //relic box
	case 0xBC1F: //relic box
	case 0xBD1F: //relic box
	case 0xBE1F: //relic box
	case 0xBF1F: //relic box
		return true;
	}
	return false;
}

static bool
dumb_test_bad(int code)
{
	switch (code) {
	case 0x011e: // exp restoration 30-70%
	// 55 int topor 0x213906
	case 0x5112: // blue pudra
	case 0x16914: // rare ore
		return false;
	}
	return true;
}

static bool
dumb_test(int code)
{
	if (dumb_test_good(code)) {
		printf(lcc_PURPLE "* Item 0x%x on the ground!" lcc_NORMAL "\n", code);
		return true;
	} else {
		return false;
	}
}

int
rfx_loot::process(rf_packet_t *pkt, pqhead_t *pre, pqhead_t *post, evqhead_t *evq)
{
	int id = -1;
	switch (pkt->type) {
	case LOOT_DROP_NEW:
		pkt->desc = "new item on the ground";
		id = GET_INT24(pkt->data + 4);
//		pkt->show = 1;
		break;
	case LOOT_DROP_HORIZON:
		pkt->desc = "new item at the vision range";
		id = GET_INT24(pkt->data + 4);
//		pkt->show = 1;
		break;
	case LOOT_DISAPPEAR:
		/* generate RFXEV_LOOT_DISAPPEAR */
		pkt->desc = "loot disappear";
//		pkt->show = 1;
		if (pkt->len >= 6) {
			unsigned ground_id = GET_INT16(pkt->data + 4);
#if 0
			if (prq && prq->gid == ground_id) {
				/* if we were trying to pick this item, generate "PICK_FAILED" */
				/* generate "pick failed" event */
				evq->push_back(new rfx_loot_pick_event(prq->gid, prq->iid, prq->code, PICK_FAILED));
				prq->release();
				prq = NULL;
			}
#endif
			/* generate "loot disappear" event */
			evq->push_back(new rfx_loot_event(ground_id));
		}
		break;
	case ITEM_PICK:
		/* we block all pick requests while we're in automatic pick mode */
		pkt->desc = "item pick request";
//		pkt->show = 1;
		if (prq) {
			/* drop this packet if automatic pick operation already in progress */
			rf_packet_t *rej = pkt_new(5, PICK_RESPONSE, SRV_TO_CLI);
			rej->data[5] = 9;	/* "error 0" */
			pqh_push(post, rej);
			pkt->drop = 1;
		}
		break;
	/* an ugly hack, but... */
	case 0x0307:
		if (prq && prq->iid == 0xffff) {
			if (pkt->len >= 16 && pkt->data[4] == 0 && prq->code == GET_INT24(pkt->data + 5)) { /* len >= 16 is taken at random */
				/* workaround for new stacks, when prq->iid is 0xffff and read iid is only given in "new item" packet */
//				printf(lcc_WHITE "*** EMITTING new stack!" lcc_NORMAL "\n");
				evq->push_back(new rfx_loot_pick_event(prq->gid, GET_INT16(pkt->data + 0x14), prq->code));
				prq->release();
				prq = NULL;
			} else if (pkt->data[4] == 9) {
				/* rate limit, resend */
				rf_packet_t *rq = pick_rq();
				rq->delay = 100; /* 100 ms delay */
				pqh_push(post, rq);
				pkt->drop = 1;
			} else {
				evq->push_back(new rfx_loot_pick_event(prq->gid, prq->iid, prq->code, PICK_FAILED));
				prq->release();
				prq = NULL;
				printf(lcc_GRAY "*** pick failure: %d" lcc_NORMAL "\n", pkt->data[4]);
			}
		}
		break;
	case PICK_RESPONSE:
		pkt->desc = "loot pick response";
//		pkt->show = 1;
		if (pkt->len >= 7 && pkt->data[4] == 0 && prq) {
			/* successful pickup */
			uint16_t iid = GET_INT16(pkt->data + 5);
			if (prq->iid == iid) {
				/* we've picked exactly what we were requested to */
//				printf("*** EMIT gid = 0x%x, iid = 0x%x, code = 0x%x\n", prq->gid, prq->iid, prq->code);
				evq->push_back(new rfx_loot_pick_event(prq->gid, prq->iid, prq->code));
				prq->release();
				prq = NULL;
			} else {
				printf(lcc_GRAY "picked 0x%x, wanted 0x%x" lcc_NORMAL "\n", iid,  prq ? prq->iid : 0xffff);
				prq->release();
				prq = NULL;
			}
		} else if (pkt->data[4] == 9 && prq) { // rate limit, huh? we'll just resend packet!
			rf_packet_t *rq = pick_rq();
//			if (prq->rq->delayed)
//				printf(lcc_RED "******************************** RESCHEDULING already scheduled packet!" lcc_NORMAL "\n");
//			else {
				/* resend packet only if it is not waiting on the queue already */
				rq->delay = 200; /* 100 ms delay */
//				printf(lcc_YELLOW "*** resending 0x%x (refc = %u)" lcc_NORMAL, prq->gid, rq->refc);
				//pkt_dump(rq);
				pqh_push(post, rq);
//			}
			pkt->drop = 1;
//			pkt->show = 1;
		} else if (prq && pkt->data[4]) {
			/* some kind of error occured */
			evq->push_back(new rfx_loot_pick_event(prq->gid, prq->iid, prq->code, PICK_FAILED));
			prq->release();
			prq = NULL;
			printf(lcc_GRAY "*** pick failure: %d" lcc_NORMAL "\n", pkt->data[4]);
		}
		break;
	}
	if (id != -1) {
		/* generate "new loot" event */
		evq->push_back(new rfx_loot_event(id, pkt->data[7] /* count */,
					GET_INT16(pkt->data + 8) /*ground_id */, pkt));
		pkt->drop = !(dumb_test(id) || lm.show.test(id));
	}
	return RFX_DECLINE;
}


std::string
rfx_loot::loot_cmd(const std::string &msg, loot_mask &m)
{
	char reply[32];
	loot_mask nm(m);

	if (msg == "info")
		goto reply;
	if (msg.compare(0, 5, "save ") == 0) {
		const char *s = msg.c_str() + 5;
		while (isspace(*s))
			s++;
		if (!*s)
			return "name required";
		std::string fn = "drop/list-";
		fn += s;
		fn += ".bin";
		return lm.show.fsave(fn.c_str())
			? std::string(s) + " saved"
			: std::string("can't save ") + s;
	}

	if (!nm.parse(msg))
		return "can't parse loot mask";

reply:
	unsigned count = nm.count();
	m = nm;
	snprintf(reply, sizeof(reply), "ok, show = %u, ignore = %u", count, 65536 - count);
	return reply;
}

int
rfx_loot::process(rfx_event *ev, pqhead_t *pre, pqhead_t *post, evqhead_t *evq)
{
	if (ev->what == RFXEV_PRIV_SENT) {
		rfx_chat_event *e = (rfx_chat_event*)ev;
		if (e->nick == "loot") {
			std::string reply = loot_cmd(e->msg, lm.show);
			evq->push_back(new rfx_chat_event(RFXEV_SEND_REPLY, e->nick, reply, NULL));
			ev->ignore_source();
			return RFX_BREAK;
		} else if (e->nick == "pick") {
			std::string reply = loot_cmd(e->msg, lm.pick);
			evq->push_back(new rfx_chat_event(RFXEV_SEND_REPLY, e->nick, reply, NULL));
			ev->ignore_source();
			return RFX_BREAK;
		}
	} else if (ev->what == RFXEV_WCHAT_SENT) {
		char cmd;
		unsigned iid, gid;
		rfx_chat_event *e = (rfx_chat_event*)ev;
		if (sscanf(e->msg.c_str(), "%c%x%x", &cmd, &iid, &gid) == 3 && cmd == 'x') {
			rf_packet_t *pi = make_pick_request(gid, iid);
//			printf("[ INJECT ]"); pkt_dump(pi);
			pqh_push(post, pi);
			ev->ignore_source();
			return RFX_BREAK;
		}
//	} else if (ev->what == RFXEV_BACKPACK_PICK) {
		/* we were requested to pick an item */
	} else if (ev->what == RFXEV_LOOT_PICK_DO) {
		/* we were requested to pick item */
//		printf(lcc_RED ">>> PICK REQEST: %u" lcc_NORMAL "\n", ((rfx_pick_do_event*)ev)->gid);
		if (!prq) {
			pqh_push(post, pick_rq((rfx_pick_do_event*)ev));
			prq = &pick_rq;
		} else {
			printf(lcc_RED "*** loot_pick_do warning: operation already in progress" lcc_NORMAL "\n");
		}
		return RFX_BREAK;
	} else if (ev->what == RFXEV_LOOT_PICK_RESET) {
		if (prq) {
			prq->release();
			prq = NULL;
		}
	}
	return RFX_DECLINE;
}

extern "C" rfx_filter *API_FILTER_ID()
{
	return new rfx_loot();
}
