﻿// SPDX-FileCopyrightText: 2020-2022 UnionTech Software Technology Co.,Ltd.

#ifndef INC_Unilang_Context_h_
#define INC_Unilang_Context_h_ 1

#include "TermAccess.h" // for ValueObject, vector, string, pair, lref,
//	TermNode, ystdex::less, map, AnchorPtr, pmr, yforward, Unilang::Deref,
//	string_view, type_info, Unilang::allocate_shared, observer_ptr,
//	EnvironmentReference, stack;
#include <ystdex/operators.hpp> // for ystdex::equality_comparable;
#include <ystdex/container.hpp> // for ystdex::try_emplace,
//	ystdex::try_emplace_hint, ystdex::insert_or_assign;
#include "BasicReduction.h" // for ReductionStatus;
#include <ystdex/functional.hpp> // for ystdex::expanded_function;
#include YFM_YSLib_Core_YEvent // for ystdex::GHEvent;
#include <cassert> // for assert;
#include <ystdex/memory.hpp> // for ystdex::make_obj_using_allocator;
#include <ystdex/swap.hpp> // for ystdex::swap_depedent;
#include <ystdex/functor.hpp> // for ystdex::ref_eq;
#include <exception> // for std::exception_ptr;
#include "Parser.h" // for ParseResultOf, ByteParser, SourcedByteParser;
#include "Lexical.h" // for LexicalAnalyzer;
#include <ystdex/ref.hpp> // for ystdex::ref, ystdex::unref;
#include <algorithm> // for std::for_each;
#include <streambuf> // for std::streambuf;
#include <istream> // for std::istream;

namespace Unilang
{

#ifndef Unilang_CheckParentEnvironment
#	ifndef NDEBUG
#		define Unilang_CheckParentEnvironment true
#	else
#		define Unilang_CheckParentEnvironment false
#	endif
#endif


using EnvironmentList = vector<ValueObject>;


class Environment final : private ystdex::equality_comparable<Environment>
{
public:
	using BindingMap = map<string, TermNode, ystdex::less<>>;
	using NameResolution
		= pair<BindingMap::mapped_type*, shared_ptr<Environment>>;
	using allocator_type = BindingMap::allocator_type;

	mutable BindingMap Bindings;
	ValueObject Parent{};
	bool Frozen = {};

private:
	AnchorPtr p_anchor{InitAnchor()};

public:
	Environment(allocator_type a)
		: Bindings(a)
	{}
	Environment(pmr::memory_resource& rsrc)
		: Environment(allocator_type(&rsrc))
	{}
	explicit
	Environment(const BindingMap& m)
		: Bindings(m)
	{}
	explicit
	Environment(BindingMap&& m)
		: Bindings(std::move(m))
	{}
	Environment(const ValueObject& vo, allocator_type a)
		: Bindings(a), Parent((CheckParent(vo), vo))
	{}
	Environment(ValueObject&& vo, allocator_type a)
		: Bindings(a), Parent((CheckParent(vo), std::move(vo)))
	{}
	Environment(pmr::memory_resource& rsrc, const ValueObject& vo)
		: Environment(vo, allocator_type(&rsrc))
	{}
	Environment(pmr::memory_resource& rsrc, ValueObject&& vo)
		: Environment(std::move(vo), allocator_type(&rsrc))
	{}
	Environment(const Environment& e)
		: Bindings(e.Bindings), Parent(e.Parent)
	{}
	Environment(Environment&&) = default;

	Environment&
	operator=(Environment&&) = default;

	YB_ATTR_nodiscard YB_PURE friend bool
	operator==(const Environment& x, const Environment& y) noexcept
	{
		return &x == &y;
	}

	YB_ATTR_nodiscard YB_PURE
	bool
	IsOrphan() const noexcept
	{
		return p_anchor.use_count() == 1;
	}

	YB_ATTR_nodiscard YB_PURE size_t
	GetAnchorCount() const noexcept
	{
		return size_t(p_anchor.use_count());
	}

	YB_ATTR_nodiscard YB_PURE const AnchorPtr&
	GetAnchorPtr() const noexcept
	{
		return p_anchor;
	}

	template<typename _tKey, typename... _tParams>
	inline ystdex::enable_if_inconvertible_t<_tKey&&,
		BindingMap::const_iterator, bool>
	AddValue(_tKey&& k, _tParams&&... args)
	{
		return ystdex::try_emplace(Bindings, yforward(k), NoContainer,
			yforward(args)...).second;
	}
	template<typename _tKey, typename... _tParams>
	inline bool
	AddValue(BindingMap::const_iterator hint, _tKey&& k, _tParams&&... args)
	{
		return ystdex::try_emplace_hint(Bindings, hint, yforward(k),
			NoContainer, yforward(args)...).second;
	}

	template<typename _tKey, class _tNode>
	TermNode&
	Bind(_tKey&& k, _tNode&& tm)
	{
		return Unilang::Deref(ystdex::insert_or_assign(Bindings, yforward(k),
			yforward(tm)).first).second;
	}

	static void
	CheckParent(const ValueObject&);

	void
	DefineChecked(string_view, ValueObject&&);

	static Environment&
	EnsureValid(const shared_ptr<Environment>&);

private:
	YB_ATTR_nodiscard YB_PURE AnchorPtr
	InitAnchor() const;

public:
	YB_ATTR_nodiscard YB_PURE NameResolution::first_type
	LookupName(string_view) const;

	YB_ATTR_nodiscard YB_PURE TermTags
	MakeTermTags(const TermNode& term) const noexcept
	{
		return Frozen ? term.Tags | TermTags::Nonmodifying : term.Tags;
	}

	YB_NORETURN static void
	ThrowForInvalidType(const type_info&);
};


class Context;

using ReducerFunctionType = ReductionStatus(Context&);

using Reducer = ystdex::expanded_function<ReducerFunctionType>;

template<class _tAlloc, class _func,
	yimpl(typename = ystdex::enable_if_same_param_t<Reducer, _func>)>
inline _func&&
ToReducer(const _tAlloc&, _func&& f)
{
	return yforward(f);
}
template<class _tAlloc, typename _tParam, typename... _tParams>
inline
	yimpl(ystdex::exclude_self_t)<Reducer, _tParam, Reducer>
ToReducer(const _tAlloc& a, _tParam&& arg, _tParams&&... args)
{
#if true
	return Reducer(std::allocator_arg, a, yforward(arg), yforward(args)...);
#else
	return ystdex::make_obj_using_allocator<Reducer>(a, yforward(arg),
		yforward(args)...);
#endif
}

using ContextAllocator = pmr::polymorphic_allocator<byte>;

// NOTE: This is the host type for combiners.
using ContextHandler = YSLib::GHEvent<ReductionStatus(TermNode&, Context&)>;


class Continuation
{
public:
	using allocator_type = ContextAllocator;

	ContextHandler Handler;

	template<typename _func, typename
		= ystdex::exclude_self_t<Continuation, _func>>
	inline
	Continuation(_func&& handler, allocator_type a)
		: Handler(ystdex::make_obj_using_allocator<ContextHandler>(a,
		yforward(handler)))
	{}
	template<typename _func, typename
		= ystdex::exclude_self_t<Continuation, _func>>
	inline
	Continuation(_func&&, const Context&);
	Continuation(const Continuation& cont, allocator_type a)
		: Handler(ystdex::make_obj_using_allocator<ContextHandler>(a,
		cont.Handler))
	{}
	Continuation(Continuation&& cont, allocator_type a)
		: Handler(ystdex::make_obj_using_allocator<ContextHandler>(a,
		std::move(cont.Handler)))
	{}
	Continuation(const Continuation&) = default;
	Continuation(Continuation&&) = default;

	Continuation&
	operator=(const Continuation&) = default;
	Continuation&
	operator=(Continuation&&) = default;

	YB_ATTR_nodiscard YB_STATELESS friend bool
	operator==(const Continuation& x, const Continuation& y) noexcept
	{
		return ystdex::ref_eq<>()(x, y);
	}

	ReductionStatus
	operator()(Context& ctx) const;
};


class GlobalState;

class Context final
{
private:
	using ReducerSequenceBase = YSLib::forward_list<Reducer>;

public:
	class ReducerSequence : public ReducerSequenceBase,
		private ystdex::equality_comparable<ReducerSequence>
	{
	public:
		ValueObject Parent{};

		using ReducerSequenceBase::ReducerSequenceBase;
		ReducerSequence(const ReducerSequence& act)
			: ReducerSequenceBase(act, act.get_allocator())
		{}
		ReducerSequence(ReducerSequence&& rs) = default;
		~ReducerSequence()
		{
			clear();
		}

		ReducerSequence&
		operator=(ReducerSequence&) = default;
		ReducerSequence&
		operator=(ReducerSequence&&) = default;

		void
		clear() noexcept
		{
			while(!empty())
				pop_front();
		}

		friend void
		swap(ReducerSequence& x, ReducerSequence& y) noexcept
		{
			assert(x.get_allocator() == y.get_allocator()
				&& "Invalid allocator found.");
			x.swap(y);
			ystdex::swap_dependent(x.Parent, y.Parent);
		}

		YB_ATTR_nodiscard YB_PURE friend bool
		operator==(const ReducerSequence& x, const ReducerSequence& y) noexcept
		{
			return ystdex::ref_eq<>()(x, y);
		}
	};

public:
	using ExceptionHandler = function<void(std::exception_ptr)>;
	class ReductionGuard
	{
	private:
		Context* p_ctx;
		ReducerSequence::const_iterator i_stacked;

	public:
		ReductionGuard(Context& ctx) noexcept
			: p_ctx(&ctx), i_stacked(ctx.stacked.cbegin())
		{
			ctx.stacked.splice_after(ctx.stacked.cbefore_begin(),
				ctx.current);
		}
		ReductionGuard(ReductionGuard&& gd) noexcept
			: p_ctx(gd.p_ctx), i_stacked(gd.i_stacked)
		{
			gd.p_ctx = {};
		}
		~ReductionGuard()
		{
			if(p_ctx)
			{
				auto& ctx(Unilang::Deref(p_ctx));

				ctx.current.splice_after(ctx.current.cbefore_begin(),
					ctx.stacked, ctx.stacked.cbefore_begin(), i_stacked);
			}
		}
	};

private:
	lref<pmr::memory_resource> memory_rsrc;
	shared_ptr<Environment> p_record{Unilang::allocate_shared<Environment>(
		Environment::allocator_type(&memory_rsrc.get()))};
	ReducerSequence
		current{ReducerSequence::allocator_type(&memory_rsrc.get())};
	ReducerSequence stacked{current.get_allocator()};

public:
	Reducer TailAction{};
	ExceptionHandler HandleException{DefaultHandleException};
	ReductionStatus LastStatus = ReductionStatus::Neutral;
	lref<const GlobalState> Global;

private:
	TermNode* next_term_ptr = {};
	TermNode* combining_term_ptr = {};

public:
	Continuation ReduceOnce{DefaultReduceOnce, *this};
	mutable ValueObject OperatorName{};
	shared_ptr<string> CurrentSource{};

	Context(const GlobalState&);

	bool
	IsAlive() const noexcept
	{
		return !current.empty();
	}

	YB_ATTR_nodiscard Environment::BindingMap&
	GetBindingsRef() const noexcept
	{
		return p_record->Bindings;
	}
	TermNode*
	GetCombiningTermPtr() const noexcept
	{
		return combining_term_ptr;
	}
	const ReducerSequence&
	GetCurrent() const noexcept
	{
		return current;
	}
	ReducerSequence&
	GetCurrentRef() noexcept
	{
		return current;
	}
	YB_ATTR_nodiscard YB_PURE TermNode&
	GetNextTermRef() const;
	YB_ATTR_nodiscard YB_PURE const shared_ptr<Environment>&
	GetRecordPtr() const noexcept
	{
		return p_record;
	}
	YB_ATTR_nodiscard YB_PURE Environment&
	GetRecordRef() const noexcept
	{
		return *p_record;
	}

	void
	SetCombiningTermRef(TermNode& term) noexcept
	{
		combining_term_ptr = &term;
	}
	void
	SetNextTermRef(TermNode& term) noexcept
	{
		next_term_ptr = &term;
	}

	template<typename _type>
	YB_ATTR_nodiscard YB_PURE _type*
	AccessCurrentAs()
	{
		return IsAlive() ? current.front().template target<_type>() : nullptr;
	}

	ReductionStatus
	ApplyTail();

	void
	ClearCombiningTerm() noexcept
	{
		combining_term_ptr = {};
	}

	YB_NORETURN static void
	DefaultHandleException(std::exception_ptr);

	// NOTE: See Evaluation.cpp for the definition.
	static ReductionStatus
	DefaultReduceOnce(TermNode&, Context&);

	ReductionStatus
	Rewrite(Reducer);

	ReductionStatus
	RewriteGuarded(TermNode&, Reducer);

	ReductionStatus
	RewriteTerm(TermNode&);

	ReductionStatus
	RewriteTermGuarded(TermNode&);

	YB_ATTR_nodiscard Environment::NameResolution
	Resolve(shared_ptr<Environment>, string_view) const;

	void
	SaveExceptionHandler()
	{
		return SetupFront(std::bind([this](ExceptionHandler& h) noexcept{
			HandleException = std::move(h);
			return LastStatus;
		}, std::move(HandleException)));
	}

	template<typename... _tParams>
	inline void
	SetupCurrent(_tParams&&... args)
	{
		assert(!IsAlive());
		return SetupFront(yforward(args)...);
	}

	template<typename... _tParams>
	inline void
	SetupFront(_tParams&&... args)
	{
		current.push_front(
			Unilang::ToReducer(get_allocator(), yforward(args)...));
	}

	template<typename... _tParams>
	void
	ShareCurrentSource(_tParams&&... args)
	{
		CurrentSource = YSLib::allocate_shared<string>(
			string::allocator_type(&memory_rsrc.get()), yforward(args)...);
	}

	YB_ATTR_nodiscard YB_PURE shared_ptr<Environment>
	ShareRecord() const noexcept;

	void
	Shift(ReducerSequence& rs, ReducerSequence::const_iterator i) noexcept
	{
		rs.splice_after(rs.cbefore_begin(), current, current.cbefore_begin(),
			i);
	}

	shared_ptr<Environment>
	SwitchEnvironment(const shared_ptr<Environment>&);

	shared_ptr<Environment>
	SwitchEnvironmentUnchecked(const shared_ptr<Environment>&) noexcept;

	YB_ATTR_nodiscard observer_ptr<TokenValue>
	TryGetTailOperatorName(TermNode& term) const noexcept
	{
		return combining_term_ptr == &term
			? TryAccessValue<TokenValue>(OperatorName) : nullptr;
	}

	bool
	TrySetTailOperatorName(TermNode&) const noexcept;

	void
	UnwindCurrent() noexcept;

	YB_ATTR_nodiscard YB_PURE EnvironmentReference
	WeakenRecord() const noexcept;

	YB_ATTR_nodiscard YB_PURE ContextAllocator
	get_allocator() const noexcept
	{
		return ContextAllocator(&memory_rsrc.get());
	}
};


template<typename _func, typename>
inline
Continuation::Continuation(_func&& handler, const Context& ctx)
	: Continuation(yforward(handler), ctx.get_allocator())
{}

inline ReductionStatus
Continuation::operator()(Context& ctx) const
{
	return Handler(ctx.GetNextTermRef(), ctx);
}


YB_NONNULL(3) inline void
AssertMatchedAllocators(const TermNode::allocator_type& a,
	const TermNode::Container& con,
	const char* msg = "Allocators mismatch to the term container.") noexcept
{
	// XXX: The message is current ignored.
	yunused(a), yunused(con), yunused(msg);
	assert(a == con.get_allocator());
}
YB_NONNULL(3) inline void
AssertMatchedAllocators(const TermNode::allocator_type& a, const TermNode& nd,
	const char* msg = "Allocators mismatch to the term node.") noexcept
{
	AssertMatchedAllocators(a, nd.GetContainer(), msg);
}
YB_NONNULL(3) inline void
AssertMatchedAllocators(const Context& ctx, const TermNode::Container& con,
	const char* msg
	= "Allocators mismatch between the term container and the context.")
	noexcept
{
	Unilang::AssertMatchedAllocators(ctx.get_allocator(), con, msg);
}
YB_NONNULL(3) inline void
AssertMatchedAllocators(const Context& ctx, const TermNode& nd,
	const char* msg
	= "Allocators mismatch between the term node and the context.") noexcept
{
	Unilang::AssertMatchedAllocators(ctx.get_allocator(), nd, msg);
}

template<typename... _tParams>
inline shared_ptr<Environment>
AllocateEnvironment(const Environment::allocator_type& a, _tParams&&... args)
{
	return Unilang::allocate_shared<Environment>(a, yforward(args)...);
}
template<typename... _tParams>
inline shared_ptr<Environment>
AllocateEnvironment(Context& ctx, _tParams&&... args)
{
	return Unilang::AllocateEnvironment(ctx.GetBindingsRef().get_allocator(),
		yforward(args)...);
}
template<typename... _tParams>
inline shared_ptr<Environment>
AllocateEnvironment(TermNode& term, Context& ctx, _tParams&&... args)
{
	const auto a(ctx.GetBindingsRef().get_allocator());

	static_cast<void>(term);
	Unilang::AssertMatchedAllocators(a, term);
	return Unilang::AllocateEnvironment(a, yforward(args)...);
}

template<typename... _tParams>
inline shared_ptr<Environment>
SwitchToFreshEnvironment(Context& ctx, _tParams&&... args)
{
	return ctx.SwitchEnvironmentUnchecked(Unilang::AllocateEnvironment(ctx,
		yforward(args)...));
}


template<typename _type, typename... _tParams>
inline bool
EmplaceLeaf(Environment::BindingMap& m, string_view name, _tParams&&... args)
{
	assert(name.data());

	const auto i(m.find(name));

	if(i == m.end())
	{
		m[string(name)].Value = _type(yforward(args)...);
		return true;
	}

	auto& nd(i->second);

	nd.Value = _type(yforward(args)...);
	nd.ClearContainer();
	return {};
}
template<typename _type, typename... _tParams>
inline bool
EmplaceLeaf(Environment& env, string_view name, _tParams&&... args)
{
	return Unilang::EmplaceLeaf<_type>(env.Bindings, name, yforward(args)...);
}
template<typename _type, typename... _tParams>
inline bool
EmplaceLeaf(Context& ctx, string_view name, _tParams&&... args)
{
	return Unilang::EmplaceLeaf<_type>(ctx.GetRecordRef(), name, yforward(args)...);
}

YB_ATTR_nodiscard inline Environment::NameResolution
ResolveName(const Context& ctx, string_view id)
{
	assert(id.data());
	return ctx.Resolve(ctx.GetRecordPtr(), id);
}

YB_ATTR_nodiscard TermNode
MoveResolved(const Context&, string_view);

YB_ATTR_nodiscard TermNode
ResolveIdentifier(const Context&, string_view);

YB_ATTR_nodiscard pair<shared_ptr<Environment>, bool>
ResolveEnvironment(const ValueObject&);
YB_ATTR_nodiscard pair<shared_ptr<Environment>, bool>
ResolveEnvironment(ValueObject&, bool);
YB_ATTR_nodiscard pair<shared_ptr<Environment>, bool>
ResolveEnvironment(const TermNode&);
YB_ATTR_nodiscard pair<shared_ptr<Environment>, bool>
ResolveEnvironment(TermNode&);

struct EnvironmentSwitcher
{
	lref<Context> ContextRef;
	mutable shared_ptr<Environment> SavedPtr;

	EnvironmentSwitcher(Context& ctx,
		shared_ptr<Environment>&& p_saved = {})
		: ContextRef(ctx), SavedPtr(std::move(p_saved))
	{}
	EnvironmentSwitcher(EnvironmentSwitcher&&) = default;

	EnvironmentSwitcher&
	operator=(EnvironmentSwitcher&&) = default;

	void
	operator()() const noexcept
	{
		if(SavedPtr)
			ContextRef.get().SwitchEnvironmentUnchecked(std::move(SavedPtr));
	}
};


template<typename _fCurrent>
inline ReductionStatus
RelaySwitched(Context& ctx, _fCurrent&& cur)
{
	ctx.SetupFront(yforward(cur));
	return ReductionStatus::Partial;
}


class SeparatorPass final
{
private:
	using TermStackEntry = pair<lref<TermNode>, bool>;
	using TermStack = stack<TermStackEntry, vector<TermStackEntry>>;
	struct TransformationSpec;

	TermNode::allocator_type allocator;
	vector<TransformationSpec> transformations;
	mutable TermStack remained{allocator};

public:
	SeparatorPass(TermNode::allocator_type);
	~SeparatorPass();

	ReductionStatus
	operator()(TermNode&) const;

	void
	Transform(TermNode&, bool, TermStack&) const;
};


template<typename _fParse>
using GParsedValue = typename ParseResultOf<_fParse>::value_type;

template<typename _fParse, typename... _tParams>
using GTokenizer
	= function<TermNode(const GParsedValue<_fParse>&, _tParams...)>;

using Tokenizer = GTokenizer<ByteParser>;

using SourcedTokenizer = GTokenizer<SourcedByteParser, Context&>;


class GlobalState
{
private:
	struct LeafConverter final
	{
		Context& ContextRef;

		YB_ATTR_nodiscard TermNode
		operator()(const GParsedValue<ByteParser>& val) const
		{
			return ContextRef.Global.get().ConvertLeaf(val);
		}
		YB_ATTR_nodiscard TermNode
		operator()(const GParsedValue<SourcedByteParser>& val) const
		{
			return ContextRef.Global.get().ConvertLeafSourced(val, ContextRef);
		}
	};

public:
	TermNode::allocator_type Allocator;
	SeparatorPass Preprocess{Allocator};
	Tokenizer ConvertLeaf;
	SourcedTokenizer ConvertLeafSourced;
	bool UseSourceLocation = {};

	GlobalState(TermNode::allocator_type = {});

	template<typename _tIn>
	YB_ATTR_nodiscard TermNode
	Prepare(LexicalAnalyzer& lexer, Context& ctx, _tIn first, _tIn last) const
	{
		ByteParser parse(lexer, Allocator);

		return Prepare(ctx, first, last, ystdex::ref(parse));
	}
	template<typename _tIn, typename _fParse>
	YB_ATTR_nodiscard TermNode
	Prepare(Context& ctx, _tIn first, _tIn last, _fParse parse) const
	{
		std::for_each(first, last, parse);

		TermNode res{Allocator};
		const auto& parse_result(ystdex::unref(parse).GetResult());

		if(ReduceSyntax(res, parse_result.cbegin(), parse_result.cend(),
			LeafConverter{ctx}) != parse_result.cend())
			throw UnilangException("Redundant ')', ']' or '}' found.");
		return res;
	}

	YB_ATTR_nodiscard TermNode
	Read(string_view, Context&);

	YB_ATTR_nodiscard TermNode
	ReadFrom(std::streambuf&, Context&) const;
	YB_ATTR_nodiscard TermNode
	ReadFrom(std::istream&, Context&) const;
};

} // namespace Unilang;

#endif

