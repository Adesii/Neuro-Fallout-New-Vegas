#pragma once

class FormID {
protected:
	union {
		struct {
			uint32_t uiID    : 24;
			uint32_t uiIndex : 8;
		} fullForm;
		struct {
			uint32_t usID			: 12;
			uint32_t usSmallIndex	: 12;
			uint32_t uiIndex		: 8;
		} compactForm;
		uint32_t uiFormID;
	};

public:
	constexpr FormID() noexcept : uiFormID(0) {}
	constexpr FormID(uint32_t auiFormID) noexcept : uiFormID(auiFormID) {}
	constexpr explicit FormID(uint8_t aucIndex, uint32_t auiID) noexcept : fullForm{auiID, aucIndex} {}

	constexpr operator uint32_t&() noexcept			{ return uiFormID; }
	constexpr operator uint32_t() const	noexcept	{ return uiFormID; }

	constexpr operator bool() const noexcept		{ return uiFormID != 0; }

	constexpr bool operator==(const FormID& other) const noexcept	{ return uiFormID == other.uiFormID; }
	constexpr bool operator<(const FormID& other) const noexcept	{ return uiFormID < other.uiFormID; }
	constexpr bool operator<=(const FormID& other) const noexcept	{ return uiFormID <= other.uiFormID; }
	constexpr bool operator>(const FormID& other) const noexcept	{ return uiFormID > other.uiFormID; }
	constexpr bool operator>=(const FormID& other) const noexcept	{ return uiFormID >= other.uiFormID; }

	template <typename T>
	constexpr bool operator==(const T& other) const noexcept requires std::is_integral_v<T>		{ return uiFormID == other; }
	template <typename T>
	constexpr bool operator<(const T& other) const noexcept requires std::is_integral_v<T>		{ return uiFormID < other; }
	template <typename T>
	constexpr  bool operator<=(const T& other) const noexcept requires std::is_integral_v<T>	{ return uiFormID <= other; }
	template <typename T>
	constexpr  bool operator>(const T& other) const noexcept requires std::is_integral_v<T>		{ return uiFormID > other; }
	template <typename T>
	constexpr  bool operator>=(const T& other) const noexcept requires std::is_integral_v<T>	{ return uiFormID >= other; }

	constexpr uint32_t		Get() const noexcept							{ return uiFormID; }
	constexpr void			Set(uint32_t auiFormID) noexcept				{ uiFormID = auiFormID; }

	constexpr uint32_t		GetID() const noexcept							{ return fullForm.uiID; }
	constexpr void			SetID(uint32_t auiID) noexcept					{ fullForm.uiID = auiID; }

	constexpr uint8_t		GetIndex() const noexcept						{ return fullForm.uiIndex; }
	constexpr void			SetIndex(uint8_t aucIndex) noexcept				{ fullForm.uiIndex = aucIndex; }

	constexpr uint16_t		GetSmallIndex() const noexcept					{ return compactForm.usSmallIndex; }
	constexpr void			SetSmallIndex(uint16_t ausSmallIndex) noexcept	{ compactForm.usSmallIndex = ausSmallIndex; }

	constexpr bool			IsRuntime() const noexcept						{ return fullForm.uiIndex == 0xFF; }
	constexpr bool			IsSmall() const noexcept						{ return fullForm.uiIndex == 0xFE; }
	constexpr bool			IsDefault() const noexcept						{ return uiFormID < 0x800; }
};
ASSERT_SIZE(FormID, 0x4);

using RefID = FormID;

class SmallFormID : public FormID {
public:
	constexpr SmallFormID() : FormID() {}
	constexpr SmallFormID(uint32_t auiFormID) : FormID(auiFormID) {}
	constexpr explicit SmallFormID(uint16_t ausSmallIndex, uint16_t ausID) : FormID() { 
		compactForm.usID			= ausID;
		compactForm.usSmallIndex	= ausSmallIndex;
		compactForm.uiIndex			= 0xFE;
	}
};
ASSERT_SIZE(SmallFormID, 0x4);
