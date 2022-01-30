#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"

#define Amm_admin_create(macro)
#define Amm_admin_view(macro)
#define Amm_admin_destroy(macro) macro(ContractID, cid)
#define Amm_admin_pools_view(macro) macro(ContractID, cid)

#define Amm_poolop(macro) \
    macro(ContractID, cid) \
    macro(AssetID, aid1) \
    macro(AssetID, aid2)

#define Amm_admin_pool_view(macro) Amm_poolop(macro)

#define AmmRole_admin(macro) \
    macro(admin, view) \
    macro(admin, create) \
    macro(admin, destroy) \
    macro(admin, pool_view) \
    macro(admin, pools_view) \

#define Amm_user_view(macro) Amm_poolop(macro)

#define Amm_user_add_liquidity(macro) \
    Amm_poolop(macro) \
    macro(Amount, val1) \
    macro(Amount, val2) \
    macro(uint32_t, bPredictOnly)

#define Amm_user_withdraw(macro) \
    Amm_poolop(macro) \
    macro(Amount, ctl) \
    macro(uint32_t, bPredictOnly)

#define Amm_user_trade(macro) \
    Amm_poolop(macro) \
    macro(Amount, val1_buy) \
    macro(uint32_t, bPredictOnly)

#define AmmRole_user(macro) \
    macro(user, view) \
    macro(user, add_liquidity) \
    macro(user, withdraw) \
    macro(user, trade) \


#define AmmRoles_All(macro) \
    macro(admin) \
    macro(user)

namespace Amm {

using MultiPrecision::Float;

BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  Amm_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); AmmRole_##name(THE_METHOD) }
        
        AmmRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(Amm_##role##_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

ON_METHOD(admin, view)
{
    EnumAndDumpContracts(s_SID);
}

ON_METHOD(admin, create)
{
    Env::GenerateKernel(nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, "create Amm contract", 0);
}

ON_METHOD(admin, destroy)
{
    Env::GenerateKernel(&cid, 1, nullptr, 0, nullptr, 0, nullptr, 0, "destroy Amm contract", 0);
}

bool SetKey(Pool::ID& pid, AssetID aid1, AssetID aid2)
{
    if (aid1 < aid2)
    {
        pid.m_Aid1 = aid1;
        pid.m_Aid2 = aid2;
    }
    else
    {
        if (aid1 == aid2)
        {
            OnError("assets must be different");
            return false;
        }

        pid.m_Aid1 = aid2;
        pid.m_Aid2 = aid1;
    }

    return true;
}

struct FloatTxt
{
    static const uint32_t s_DigsAfterDotMax = 18;
    static const uint32_t s_TxtLenMax = Utils::String::Decimal::DigitsMax<uint32_t>::N + s_DigsAfterDotMax + 6; // 1st dig, dot, space, E, space, minus

    static uint32_t Print_(char* szBuf, Float x, uint32_t nDigitsAfterDot)
    {
        if (x.IsZero())
        {
            szBuf[0] = '0';
            szBuf[1] = 0;
            return 1;
        }

        if (nDigitsAfterDot > s_DigsAfterDotMax)
            nDigitsAfterDot = s_DigsAfterDotMax;

        uint64_t trgLo = 1;
        for (uint32_t i = 0; i < nDigitsAfterDot; i++)
            trgLo *= 10;

        uint64_t trgHi = trgLo * 10;

        int ord = nDigitsAfterDot;

        Float one(1u);
        Float dec(10u);

        uint64_t x_ = x.Get();
        if (x_ < trgLo)
        {
            do
            {
                x = x * dec;
                ord--;
            } while ((x_ = x.Get()) < trgLo);

            if (x_ >= trgHi)
            {
                x_ /= 10;
                ord++;
            }
        }
        else
        {
            if (x_ >= trgHi)
            {
                Float dec_inv = one / dec;

                do
                {
                    x = x * dec_inv;
                    ord++;
                } while ((x_ = x.Get()) >= trgHi);

                if (x_ < trgLo)
                {
                    x_ *= 10;
                    ord--;
                }
            }
        }

        uint32_t nPos = 0;

        szBuf[nPos++] = '0' + (x_ / trgLo);
        szBuf[nPos++] = '.';

        Utils::String::Decimal::Print(szBuf + nPos, x_ - trgLo, nDigitsAfterDot);
        nPos += nDigitsAfterDot;

        if (ord)
        {
            szBuf[nPos++] = ' ';
            szBuf[nPos++] = 'E';

            if (ord > 0)
                szBuf[nPos++] = ' ';
            else
            {
                ord = -ord;
                szBuf[nPos++] = '-';
            }

            nPos += Utils::String::Decimal::Print(szBuf + nPos, ord);
        }

        szBuf[nPos] = 0;
        return nPos;
    }

    char m_sz[s_TxtLenMax + 1];
    uint32_t Print(Float f, uint32_t nDigitsAfterDot = 10)
    {
        return Print_(m_sz, f, nDigitsAfterDot);
    }
};

void DocAddRate(const char* sz, Amount v1, Amount v2)
{
    FloatTxt ftxt;
    ftxt.Print(Float(v1) / Float(v2));
    Env::DocAddText(sz, ftxt.m_sz);
}

struct PoolsWalker
{
    Env::VarReaderEx<true> m_R;
    Env::Key_T<Pool::Key> m_Key;
    Pool m_Pool;

    void Enum(const ContractID& cid)
    {
        _POD_(m_Key.m_Prefix.m_Cid) = cid;
        _POD_(m_Key.m_KeyInContract.m_ID).SetZero();

        Env::Key_T<Pool::Key> key2;
        _POD_(key2.m_Prefix.m_Cid) = cid;
        _POD_(key2.m_KeyInContract.m_ID).SetObject(0xff);

        m_R.Enum_T(m_Key, key2);
    }

    void Enum(const ContractID& cid, const Pool::ID& pid)
    {
        _POD_(m_Key.m_Prefix.m_Cid) = cid;
        m_Key.m_KeyInContract.m_ID = pid;

        m_R.Enum_T(m_Key, m_Key);
    }

    bool Move()
    {
        return m_R.MoveNext_T(m_Key, m_Pool);
    }

    static void PrintTotals(const Totals& x)
    {
        Env::DocAddNum("ctl", x.m_Ctl);
        Env::DocAddNum("tok1", x.m_Tok1);
        Env::DocAddNum("tok2", x.m_Tok2);
    }

    void PrintPool() const
    {
        PrintTotals(m_Pool.m_Totals);

        if (m_Pool.m_Totals.m_Ctl)
        {
            DocAddRate("k1_2", m_Pool.m_Totals.m_Tok1, m_Pool.m_Totals.m_Tok2);
            DocAddRate("k2_1", m_Pool.m_Totals.m_Tok2, m_Pool.m_Totals.m_Tok1);
        }
    }

    void PrintKey() const
    {
        Env::DocAddNum("aid1", m_Key.m_KeyInContract.m_ID.m_Aid1);
        Env::DocAddNum("aid2", m_Key.m_KeyInContract.m_ID.m_Aid2);
    }

};

ON_METHOD(admin, pools_view)
{
    Env::DocArray gr("res");

    PoolsWalker pw;
    for (pw.Enum(cid); pw.Move(); )
    {
        Env::DocGroup gr1("");

        pw.PrintKey();
        pw.PrintPool();
    }
}

ON_METHOD(admin, pool_view)
{
    Pool::ID pid;
    if (!SetKey(pid, aid1, aid2))
        return;

    PoolsWalker pw;
    pw.Enum(cid, pid);
    if (pw.Move())
    {
        Env::DocGroup gr("res");
        pw.PrintPool();
    }
    else
        OnError("no such a pool");
}

#pragma pack (push, 1)
struct UserKeyMaterial
{
    ContractID m_Cid;
    Pool::ID m_Pid;
};
#pragma pack (pop)

ON_METHOD(user, view)
{
    PoolsWalker pw;

    bool bSpecific = (aid1 || aid2);
    if (bSpecific)
    {
        Pool::ID pid;
        if (!SetKey(pid, aid1, aid2))
            return;

        pw.Enum(cid, pid);
    }
    else
        pw.Enum(cid);

    UserKeyMaterial ukm;
    _POD_(ukm.m_Cid) = cid;

    Env::Key_T<User::Key> uk;
    _POD_(uk.m_Prefix.m_Cid) = cid;

    Env::DocArray gr("res");

    while (pw.Move())
    {
        ukm.m_Pid = pw.m_Key.m_KeyInContract.m_ID;

        uk.m_KeyInContract.m_ID.m_Pid = pw.m_Key.m_KeyInContract.m_ID;
        Env::DerivePk(uk.m_KeyInContract.m_ID.m_pk, &ukm, sizeof(ukm));

        User u;
        if (Env::VarReader::Read_T(uk, u))
        {
            assert(u.m_Ctl);

            Env::DocGroup gr1("");

            if (!bSpecific)
                pw.PrintKey();

            Totals x;
            x.m_Ctl = u.m_Ctl;
            Cast::Down<Amounts>(x) = pw.m_Pool.m_Totals.Remove(x.m_Ctl);

            pw.PrintTotals(x);
        }
    }
}

ON_METHOD(user, add_liquidity)
{
}

ON_METHOD(user, withdraw)
{
}

ON_METHOD(user, trade)
{
}

#undef ON_METHOD
#undef THE_FIELD

BEAM_EXPORT void Method_1() 
{
    Env::DocGroup root("");

    char szRole[0x10], szAction[0x10];

    if (!Env::DocGetText("role", szRole, sizeof(szRole)))
        return OnError("Role not specified");

    if (!Env::DocGetText("action", szAction, sizeof(szAction)))
        return OnError("Action not specified");

    const char* szErr = nullptr;

#define PAR_READ(type, name) type arg_##name; Env::DocGet(#name, arg_##name);
#define PAR_PASS(type, name) arg_##name,

#define THE_METHOD(role, name) \
        if (!Env::Strcmp(szAction, #name)) { \
            Amm_##role##_##name(PAR_READ) \
            On_##role##_##name(Amm_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        AmmRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    AmmRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

} // namespace Amm
