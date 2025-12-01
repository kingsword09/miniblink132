#ifndef SRC_TRACE_PROCESSOR_TABLES_PERF_TABLES_PY_H_
#define SRC_TRACE_PROCESSOR_TABLES_PERF_TABLES_PY_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/ref_counted.h"
#include "src/trace_processor/containers/bit_vector.h"
#include "src/trace_processor/containers/row_map.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/db/column/arrangement_overlay.h"
#include "src/trace_processor/db/column/data_layer.h"
#include "src/trace_processor/db/column/dense_null_overlay.h"
#include "src/trace_processor/db/column/numeric_storage.h"
#include "src/trace_processor/db/column/id_storage.h"
#include "src/trace_processor/db/column/null_overlay.h"
#include "src/trace_processor/db/column/range_overlay.h"
#include "src/trace_processor/db/column/selector_overlay.h"
#include "src/trace_processor/db/column/set_id_storage.h"
#include "src/trace_processor/db/column/string_storage.h"
#include "src/trace_processor/db/column/types.h"
#include "src/trace_processor/db/column_storage.h"
#include "src/trace_processor/db/column.h"
#include "src/trace_processor/db/table.h"
#include "src/trace_processor/db/typed_column.h"
#include "src/trace_processor/db/typed_column_internal.h"
#include "src/trace_processor/tables/macros_internal.h"

#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/tables/profiler_tables_py.h"

namespace perfetto::trace_processor::tables {

class SpeRecordTable : public macros_internal::MacroTable {
 public:
  static constexpr uint32_t kColumnCount = 14;

  struct Id : public BaseId {
    Id() = default;
    explicit constexpr Id(uint32_t v) : BaseId(v) {}
  };
  static_assert(std::is_trivially_destructible_v<Id>,
                "Inheritance used without trivial destruction");
    
  struct ColumnIndex {
    static constexpr uint32_t id = 0;
    static constexpr uint32_t type = 1;
    static constexpr uint32_t ts = 2;
    static constexpr uint32_t utid = 3;
    static constexpr uint32_t exception_level = 4;
    static constexpr uint32_t instruction_frame_id = 5;
    static constexpr uint32_t operation = 6;
    static constexpr uint32_t data_virtual_address = 7;
    static constexpr uint32_t data_physical_address = 8;
    static constexpr uint32_t total_latency = 9;
    static constexpr uint32_t issue_latency = 10;
    static constexpr uint32_t translation_latency = 11;
    static constexpr uint32_t events_bitmask = 12;
    static constexpr uint32_t data_source = 13;
  };
  struct ColumnType {
    using id = IdColumn<SpeRecordTable::Id>;
    using type = TypedColumn<StringPool::Id>;
    using ts = TypedColumn<int64_t>;
    using utid = TypedColumn<std::optional<uint32_t>>;
    using exception_level = TypedColumn<StringPool::Id>;
    using instruction_frame_id = TypedColumn<std::optional<StackProfileFrameTable::Id>>;
    using operation = TypedColumn<StringPool::Id>;
    using data_virtual_address = TypedColumn<int64_t>;
    using data_physical_address = TypedColumn<int64_t>;
    using total_latency = TypedColumn<uint32_t>;
    using issue_latency = TypedColumn<uint32_t>;
    using translation_latency = TypedColumn<uint32_t>;
    using events_bitmask = TypedColumn<int64_t>;
    using data_source = TypedColumn<StringPool::Id>;
  };
  struct Row : public macros_internal::RootParentTable::Row {
    Row(int64_t in_ts = {},
        std::optional<uint32_t> in_utid = {},
        StringPool::Id in_exception_level = {},
        std::optional<StackProfileFrameTable::Id> in_instruction_frame_id = {},
        StringPool::Id in_operation = {},
        int64_t in_data_virtual_address = {},
        int64_t in_data_physical_address = {},
        uint32_t in_total_latency = {},
        uint32_t in_issue_latency = {},
        uint32_t in_translation_latency = {},
        int64_t in_events_bitmask = {},
        StringPool::Id in_data_source = {},
        std::nullptr_t = nullptr)
        : macros_internal::RootParentTable::Row(),
          ts(in_ts),
          utid(in_utid),
          exception_level(in_exception_level),
          instruction_frame_id(in_instruction_frame_id),
          operation(in_operation),
          data_virtual_address(in_data_virtual_address),
          data_physical_address(in_data_physical_address),
          total_latency(in_total_latency),
          issue_latency(in_issue_latency),
          translation_latency(in_translation_latency),
          events_bitmask(in_events_bitmask),
          data_source(in_data_source) {
      type_ = "__intrinsic_spe_record";
    }
    int64_t ts;
    std::optional<uint32_t> utid;
    StringPool::Id exception_level;
    std::optional<StackProfileFrameTable::Id> instruction_frame_id;
    StringPool::Id operation;
    int64_t data_virtual_address;
    int64_t data_physical_address;
    uint32_t total_latency;
    uint32_t issue_latency;
    uint32_t translation_latency;
    int64_t events_bitmask;
    StringPool::Id data_source;

    bool operator==(const SpeRecordTable::Row& other) const {
      return type() == other.type() && ColumnType::ts::Equals(ts, other.ts) &&
       ColumnType::utid::Equals(utid, other.utid) &&
       ColumnType::exception_level::Equals(exception_level, other.exception_level) &&
       ColumnType::instruction_frame_id::Equals(instruction_frame_id, other.instruction_frame_id) &&
       ColumnType::operation::Equals(operation, other.operation) &&
       ColumnType::data_virtual_address::Equals(data_virtual_address, other.data_virtual_address) &&
       ColumnType::data_physical_address::Equals(data_physical_address, other.data_physical_address) &&
       ColumnType::total_latency::Equals(total_latency, other.total_latency) &&
       ColumnType::issue_latency::Equals(issue_latency, other.issue_latency) &&
       ColumnType::translation_latency::Equals(translation_latency, other.translation_latency) &&
       ColumnType::events_bitmask::Equals(events_bitmask, other.events_bitmask) &&
       ColumnType::data_source::Equals(data_source, other.data_source);
    }
  };
  struct ColumnFlag {
    static constexpr uint32_t ts = static_cast<uint32_t>(ColumnLegacy::Flag::kSorted) | ColumnType::ts::default_flags();
    static constexpr uint32_t utid = ColumnType::utid::default_flags();
    static constexpr uint32_t exception_level = ColumnType::exception_level::default_flags();
    static constexpr uint32_t instruction_frame_id = ColumnType::instruction_frame_id::default_flags();
    static constexpr uint32_t operation = ColumnType::operation::default_flags();
    static constexpr uint32_t data_virtual_address = ColumnType::data_virtual_address::default_flags();
    static constexpr uint32_t data_physical_address = ColumnType::data_physical_address::default_flags();
    static constexpr uint32_t total_latency = ColumnType::total_latency::default_flags();
    static constexpr uint32_t issue_latency = ColumnType::issue_latency::default_flags();
    static constexpr uint32_t translation_latency = ColumnType::translation_latency::default_flags();
    static constexpr uint32_t events_bitmask = ColumnType::events_bitmask::default_flags();
    static constexpr uint32_t data_source = ColumnType::data_source::default_flags();
  };

  class RowNumber;
  class ConstRowReference;
  class RowReference;

  class RowNumber : public macros_internal::AbstractRowNumber<
      SpeRecordTable, ConstRowReference, RowReference> {
   public:
    explicit RowNumber(uint32_t row_number)
        : AbstractRowNumber(row_number) {}
  };
  static_assert(std::is_trivially_destructible_v<RowNumber>,
                "Inheritance used without trivial destruction");

  class ConstRowReference : public macros_internal::AbstractConstRowReference<
    SpeRecordTable, RowNumber> {
   public:
    ConstRowReference(const SpeRecordTable* table, uint32_t row_number)
        : AbstractConstRowReference(table, row_number) {}

    ColumnType::id::type id() const {
      return table()->id()[row_number_];
    }
    ColumnType::type::type type() const {
      return table()->type()[row_number_];
    }
    ColumnType::ts::type ts() const {
      return table()->ts()[row_number_];
    }
    ColumnType::utid::type utid() const {
      return table()->utid()[row_number_];
    }
    ColumnType::exception_level::type exception_level() const {
      return table()->exception_level()[row_number_];
    }
    ColumnType::instruction_frame_id::type instruction_frame_id() const {
      return table()->instruction_frame_id()[row_number_];
    }
    ColumnType::operation::type operation() const {
      return table()->operation()[row_number_];
    }
    ColumnType::data_virtual_address::type data_virtual_address() const {
      return table()->data_virtual_address()[row_number_];
    }
    ColumnType::data_physical_address::type data_physical_address() const {
      return table()->data_physical_address()[row_number_];
    }
    ColumnType::total_latency::type total_latency() const {
      return table()->total_latency()[row_number_];
    }
    ColumnType::issue_latency::type issue_latency() const {
      return table()->issue_latency()[row_number_];
    }
    ColumnType::translation_latency::type translation_latency() const {
      return table()->translation_latency()[row_number_];
    }
    ColumnType::events_bitmask::type events_bitmask() const {
      return table()->events_bitmask()[row_number_];
    }
    ColumnType::data_source::type data_source() const {
      return table()->data_source()[row_number_];
    }
  };
  static_assert(std::is_trivially_destructible_v<ConstRowReference>,
                "Inheritance used without trivial destruction");
  class RowReference : public ConstRowReference {
   public:
    RowReference(const SpeRecordTable* table, uint32_t row_number)
        : ConstRowReference(table, row_number) {}

    void set_ts(
        ColumnType::ts::non_optional_type v) {
      return mutable_table()->mutable_ts()->Set(row_number_, v);
    }
    void set_utid(
        ColumnType::utid::non_optional_type v) {
      return mutable_table()->mutable_utid()->Set(row_number_, v);
    }
    void set_exception_level(
        ColumnType::exception_level::non_optional_type v) {
      return mutable_table()->mutable_exception_level()->Set(row_number_, v);
    }
    void set_instruction_frame_id(
        ColumnType::instruction_frame_id::non_optional_type v) {
      return mutable_table()->mutable_instruction_frame_id()->Set(row_number_, v);
    }
    void set_operation(
        ColumnType::operation::non_optional_type v) {
      return mutable_table()->mutable_operation()->Set(row_number_, v);
    }
    void set_data_virtual_address(
        ColumnType::data_virtual_address::non_optional_type v) {
      return mutable_table()->mutable_data_virtual_address()->Set(row_number_, v);
    }
    void set_data_physical_address(
        ColumnType::data_physical_address::non_optional_type v) {
      return mutable_table()->mutable_data_physical_address()->Set(row_number_, v);
    }
    void set_total_latency(
        ColumnType::total_latency::non_optional_type v) {
      return mutable_table()->mutable_total_latency()->Set(row_number_, v);
    }
    void set_issue_latency(
        ColumnType::issue_latency::non_optional_type v) {
      return mutable_table()->mutable_issue_latency()->Set(row_number_, v);
    }
    void set_translation_latency(
        ColumnType::translation_latency::non_optional_type v) {
      return mutable_table()->mutable_translation_latency()->Set(row_number_, v);
    }
    void set_events_bitmask(
        ColumnType::events_bitmask::non_optional_type v) {
      return mutable_table()->mutable_events_bitmask()->Set(row_number_, v);
    }
    void set_data_source(
        ColumnType::data_source::non_optional_type v) {
      return mutable_table()->mutable_data_source()->Set(row_number_, v);
    }

   private:
    SpeRecordTable* mutable_table() const {
      return const_cast<SpeRecordTable*>(table());
    }
  };
  static_assert(std::is_trivially_destructible_v<RowReference>,
                "Inheritance used without trivial destruction");

  class ConstIterator;
  class ConstIterator : public macros_internal::AbstractConstIterator<
    ConstIterator, SpeRecordTable, RowNumber, ConstRowReference> {
   public:
    ColumnType::id::type id() const {
      const auto& col = table()->id();
      return col.GetAtIdx(
        iterator_.StorageIndexForColumn(col.index_in_table()));
    }
    ColumnType::type::type type() const {
      const auto& col = table()->type();
      return col.GetAtIdx(
        iterator_.StorageIndexForColumn(col.index_in_table()));
    }
    ColumnType::ts::type ts() const {
      const auto& col = table()->ts();
      return col.GetAtIdx(
        iterator_.StorageIndexForColumn(col.index_in_table()));
    }
    ColumnType::utid::type utid() const {
      const auto& col = table()->utid();
      return col.GetAtIdx(
        iterator_.StorageIndexForColumn(col.index_in_table()));
    }
    ColumnType::exception_level::type exception_level() const {
      const auto& col = table()->exception_level();
      return col.GetAtIdx(
        iterator_.StorageIndexForColumn(col.index_in_table()));
    }
    ColumnType::instruction_frame_id::type instruction_frame_id() const {
      const auto& col = table()->instruction_frame_id();
      return col.GetAtIdx(
        iterator_.StorageIndexForColumn(col.index_in_table()));
    }
    ColumnType::operation::type operation() const {
      const auto& col = table()->operation();
      return col.GetAtIdx(
        iterator_.StorageIndexForColumn(col.index_in_table()));
    }
    ColumnType::data_virtual_address::type data_virtual_address() const {
      const auto& col = table()->data_virtual_address();
      return col.GetAtIdx(
        iterator_.StorageIndexForColumn(col.index_in_table()));
    }
    ColumnType::data_physical_address::type data_physical_address() const {
      const auto& col = table()->data_physical_address();
      return col.GetAtIdx(
        iterator_.StorageIndexForColumn(col.index_in_table()));
    }
    ColumnType::total_latency::type total_latency() const {
      const auto& col = table()->total_latency();
      return col.GetAtIdx(
        iterator_.StorageIndexForColumn(col.index_in_table()));
    }
    ColumnType::issue_latency::type issue_latency() const {
      const auto& col = table()->issue_latency();
      return col.GetAtIdx(
        iterator_.StorageIndexForColumn(col.index_in_table()));
    }
    ColumnType::translation_latency::type translation_latency() const {
      const auto& col = table()->translation_latency();
      return col.GetAtIdx(
        iterator_.StorageIndexForColumn(col.index_in_table()));
    }
    ColumnType::events_bitmask::type events_bitmask() const {
      const auto& col = table()->events_bitmask();
      return col.GetAtIdx(
        iterator_.StorageIndexForColumn(col.index_in_table()));
    }
    ColumnType::data_source::type data_source() const {
      const auto& col = table()->data_source();
      return col.GetAtIdx(
        iterator_.StorageIndexForColumn(col.index_in_table()));
    }

   protected:
    explicit ConstIterator(const SpeRecordTable* table,
                           Table::Iterator iterator)
        : AbstractConstIterator(table, std::move(iterator)) {}

    uint32_t CurrentRowNumber() const {
      return iterator_.StorageIndexForLastOverlay();
    }

   private:
    friend class SpeRecordTable;
    friend class macros_internal::AbstractConstIterator<
      ConstIterator, SpeRecordTable, RowNumber, ConstRowReference>;
  };
  class Iterator : public ConstIterator {
    public:
     RowReference row_reference() const {
       return {const_cast<SpeRecordTable*>(table()), CurrentRowNumber()};
     }

    private:
     friend class SpeRecordTable;

     explicit Iterator(SpeRecordTable* table, Table::Iterator iterator)
        : ConstIterator(table, std::move(iterator)) {}
  };

  struct IdAndRow {
    Id id;
    uint32_t row;
    RowReference row_reference;
    RowNumber row_number;
  };

  static std::vector<ColumnLegacy> GetColumns(
      SpeRecordTable* self,
      const macros_internal::MacroTable* parent) {
    std::vector<ColumnLegacy> columns =
        CopyColumnsFromParentOrAddRootColumns(self, parent);
    uint32_t olay_idx = OverlayCount(parent);
    AddColumnToVector(columns, "ts", &self->ts_, ColumnFlag::ts,
                      static_cast<uint32_t>(columns.size()), olay_idx);
    AddColumnToVector(columns, "utid", &self->utid_, ColumnFlag::utid,
                      static_cast<uint32_t>(columns.size()), olay_idx);
    AddColumnToVector(columns, "exception_level", &self->exception_level_, ColumnFlag::exception_level,
                      static_cast<uint32_t>(columns.size()), olay_idx);
    AddColumnToVector(columns, "instruction_frame_id", &self->instruction_frame_id_, ColumnFlag::instruction_frame_id,
                      static_cast<uint32_t>(columns.size()), olay_idx);
    AddColumnToVector(columns, "operation", &self->operation_, ColumnFlag::operation,
                      static_cast<uint32_t>(columns.size()), olay_idx);
    AddColumnToVector(columns, "data_virtual_address", &self->data_virtual_address_, ColumnFlag::data_virtual_address,
                      static_cast<uint32_t>(columns.size()), olay_idx);
    AddColumnToVector(columns, "data_physical_address", &self->data_physical_address_, ColumnFlag::data_physical_address,
                      static_cast<uint32_t>(columns.size()), olay_idx);
    AddColumnToVector(columns, "total_latency", &self->total_latency_, ColumnFlag::total_latency,
                      static_cast<uint32_t>(columns.size()), olay_idx);
    AddColumnToVector(columns, "issue_latency", &self->issue_latency_, ColumnFlag::issue_latency,
                      static_cast<uint32_t>(columns.size()), olay_idx);
    AddColumnToVector(columns, "translation_latency", &self->translation_latency_, ColumnFlag::translation_latency,
                      static_cast<uint32_t>(columns.size()), olay_idx);
    AddColumnToVector(columns, "events_bitmask", &self->events_bitmask_, ColumnFlag::events_bitmask,
                      static_cast<uint32_t>(columns.size()), olay_idx);
    AddColumnToVector(columns, "data_source", &self->data_source_, ColumnFlag::data_source,
                      static_cast<uint32_t>(columns.size()), olay_idx);
    return columns;
  }

  PERFETTO_NO_INLINE explicit SpeRecordTable(StringPool* pool)
      : macros_internal::MacroTable(
          pool,
          GetColumns(this, nullptr),
          nullptr),
        ts_(ColumnStorage<ColumnType::ts::stored_type>::Create<false>()),
        utid_(ColumnStorage<ColumnType::utid::stored_type>::Create<false>()),
        exception_level_(ColumnStorage<ColumnType::exception_level::stored_type>::Create<false>()),
        instruction_frame_id_(ColumnStorage<ColumnType::instruction_frame_id::stored_type>::Create<false>()),
        operation_(ColumnStorage<ColumnType::operation::stored_type>::Create<false>()),
        data_virtual_address_(ColumnStorage<ColumnType::data_virtual_address::stored_type>::Create<false>()),
        data_physical_address_(ColumnStorage<ColumnType::data_physical_address::stored_type>::Create<false>()),
        total_latency_(ColumnStorage<ColumnType::total_latency::stored_type>::Create<false>()),
        issue_latency_(ColumnStorage<ColumnType::issue_latency::stored_type>::Create<false>()),
        translation_latency_(ColumnStorage<ColumnType::translation_latency::stored_type>::Create<false>()),
        events_bitmask_(ColumnStorage<ColumnType::events_bitmask::stored_type>::Create<false>()),
        data_source_(ColumnStorage<ColumnType::data_source::stored_type>::Create<false>())
,
        id_storage_layer_(new column::IdStorage()),
        type_storage_layer_(
          new column::StringStorage(string_pool(), &type_.vector())),
        ts_storage_layer_(
        new column::NumericStorage<ColumnType::ts::non_optional_stored_type>(
          &ts_.vector(),
          ColumnTypeHelper<ColumnType::ts::stored_type>::ToColumnType(),
          true)),
        utid_storage_layer_(
          new column::NumericStorage<ColumnType::utid::non_optional_stored_type>(
            &utid_.non_null_vector(),
            ColumnTypeHelper<ColumnType::utid::stored_type>::ToColumnType(),
            false)),
        exception_level_storage_layer_(
          new column::StringStorage(string_pool(), &exception_level_.vector())),
        instruction_frame_id_storage_layer_(
          new column::NumericStorage<ColumnType::instruction_frame_id::non_optional_stored_type>(
            &instruction_frame_id_.non_null_vector(),
            ColumnTypeHelper<ColumnType::instruction_frame_id::stored_type>::ToColumnType(),
            false)),
        operation_storage_layer_(
          new column::StringStorage(string_pool(), &operation_.vector())),
        data_virtual_address_storage_layer_(
        new column::NumericStorage<ColumnType::data_virtual_address::non_optional_stored_type>(
          &data_virtual_address_.vector(),
          ColumnTypeHelper<ColumnType::data_virtual_address::stored_type>::ToColumnType(),
          false)),
        data_physical_address_storage_layer_(
        new column::NumericStorage<ColumnType::data_physical_address::non_optional_stored_type>(
          &data_physical_address_.vector(),
          ColumnTypeHelper<ColumnType::data_physical_address::stored_type>::ToColumnType(),
          false)),
        total_latency_storage_layer_(
        new column::NumericStorage<ColumnType::total_latency::non_optional_stored_type>(
          &total_latency_.vector(),
          ColumnTypeHelper<ColumnType::total_latency::stored_type>::ToColumnType(),
          false)),
        issue_latency_storage_layer_(
        new column::NumericStorage<ColumnType::issue_latency::non_optional_stored_type>(
          &issue_latency_.vector(),
          ColumnTypeHelper<ColumnType::issue_latency::stored_type>::ToColumnType(),
          false)),
        translation_latency_storage_layer_(
        new column::NumericStorage<ColumnType::translation_latency::non_optional_stored_type>(
          &translation_latency_.vector(),
          ColumnTypeHelper<ColumnType::translation_latency::stored_type>::ToColumnType(),
          false)),
        events_bitmask_storage_layer_(
        new column::NumericStorage<ColumnType::events_bitmask::non_optional_stored_type>(
          &events_bitmask_.vector(),
          ColumnTypeHelper<ColumnType::events_bitmask::stored_type>::ToColumnType(),
          false)),
        data_source_storage_layer_(
          new column::StringStorage(string_pool(), &data_source_.vector()))
,
        utid_null_layer_(new column::NullOverlay(utid_.bv())),
        instruction_frame_id_null_layer_(new column::NullOverlay(instruction_frame_id_.bv())) {
    static_assert(
        ColumnLegacy::IsFlagsAndTypeValid<ColumnType::ts::stored_type>(
          ColumnFlag::ts),
        "Column type and flag combination is not valid");
      static_assert(
        ColumnLegacy::IsFlagsAndTypeValid<ColumnType::utid::stored_type>(
          ColumnFlag::utid),
        "Column type and flag combination is not valid");
      static_assert(
        ColumnLegacy::IsFlagsAndTypeValid<ColumnType::exception_level::stored_type>(
          ColumnFlag::exception_level),
        "Column type and flag combination is not valid");
      static_assert(
        ColumnLegacy::IsFlagsAndTypeValid<ColumnType::instruction_frame_id::stored_type>(
          ColumnFlag::instruction_frame_id),
        "Column type and flag combination is not valid");
      static_assert(
        ColumnLegacy::IsFlagsAndTypeValid<ColumnType::operation::stored_type>(
          ColumnFlag::operation),
        "Column type and flag combination is not valid");
      static_assert(
        ColumnLegacy::IsFlagsAndTypeValid<ColumnType::data_virtual_address::stored_type>(
          ColumnFlag::data_virtual_address),
        "Column type and flag combination is not valid");
      static_assert(
        ColumnLegacy::IsFlagsAndTypeValid<ColumnType::data_physical_address::stored_type>(
          ColumnFlag::data_physical_address),
        "Column type and flag combination is not valid");
      static_assert(
        ColumnLegacy::IsFlagsAndTypeValid<ColumnType::total_latency::stored_type>(
          ColumnFlag::total_latency),
        "Column type and flag combination is not valid");
      static_assert(
        ColumnLegacy::IsFlagsAndTypeValid<ColumnType::issue_latency::stored_type>(
          ColumnFlag::issue_latency),
        "Column type and flag combination is not valid");
      static_assert(
        ColumnLegacy::IsFlagsAndTypeValid<ColumnType::translation_latency::stored_type>(
          ColumnFlag::translation_latency),
        "Column type and flag combination is not valid");
      static_assert(
        ColumnLegacy::IsFlagsAndTypeValid<ColumnType::events_bitmask::stored_type>(
          ColumnFlag::events_bitmask),
        "Column type and flag combination is not valid");
      static_assert(
        ColumnLegacy::IsFlagsAndTypeValid<ColumnType::data_source::stored_type>(
          ColumnFlag::data_source),
        "Column type and flag combination is not valid");
    OnConstructionCompletedRegularConstructor(
      {id_storage_layer_,type_storage_layer_,ts_storage_layer_,utid_storage_layer_,exception_level_storage_layer_,instruction_frame_id_storage_layer_,operation_storage_layer_,data_virtual_address_storage_layer_,data_physical_address_storage_layer_,total_latency_storage_layer_,issue_latency_storage_layer_,translation_latency_storage_layer_,events_bitmask_storage_layer_,data_source_storage_layer_},
      {{},{},{},utid_null_layer_,{},instruction_frame_id_null_layer_,{},{},{},{},{},{},{},{}});
  }
  ~SpeRecordTable() override;

  static const char* Name() { return "__intrinsic_spe_record"; }

  static Table::Schema ComputeStaticSchema() {
    Table::Schema schema;
    schema.columns.emplace_back(Table::Schema::Column{
        "id", SqlValue::Type::kLong, true, true, false, false});
    schema.columns.emplace_back(Table::Schema::Column{
        "type", SqlValue::Type::kString, false, false, false, false});
    schema.columns.emplace_back(Table::Schema::Column{
        "ts", ColumnType::ts::SqlValueType(), false,
        true,
        false,
        false});
    schema.columns.emplace_back(Table::Schema::Column{
        "utid", ColumnType::utid::SqlValueType(), false,
        false,
        false,
        false});
    schema.columns.emplace_back(Table::Schema::Column{
        "exception_level", ColumnType::exception_level::SqlValueType(), false,
        false,
        false,
        false});
    schema.columns.emplace_back(Table::Schema::Column{
        "instruction_frame_id", ColumnType::instruction_frame_id::SqlValueType(), false,
        false,
        false,
        false});
    schema.columns.emplace_back(Table::Schema::Column{
        "operation", ColumnType::operation::SqlValueType(), false,
        false,
        false,
        false});
    schema.columns.emplace_back(Table::Schema::Column{
        "data_virtual_address", ColumnType::data_virtual_address::SqlValueType(), false,
        false,
        false,
        false});
    schema.columns.emplace_back(Table::Schema::Column{
        "data_physical_address", ColumnType::data_physical_address::SqlValueType(), false,
        false,
        false,
        false});
    schema.columns.emplace_back(Table::Schema::Column{
        "total_latency", ColumnType::total_latency::SqlValueType(), false,
        false,
        false,
        false});
    schema.columns.emplace_back(Table::Schema::Column{
        "issue_latency", ColumnType::issue_latency::SqlValueType(), false,
        false,
        false,
        false});
    schema.columns.emplace_back(Table::Schema::Column{
        "translation_latency", ColumnType::translation_latency::SqlValueType(), false,
        false,
        false,
        false});
    schema.columns.emplace_back(Table::Schema::Column{
        "events_bitmask", ColumnType::events_bitmask::SqlValueType(), false,
        false,
        false,
        false});
    schema.columns.emplace_back(Table::Schema::Column{
        "data_source", ColumnType::data_source::SqlValueType(), false,
        false,
        false,
        false});
    return schema;
  }

  ConstIterator IterateRows() const {
    return ConstIterator(this, Table::IterateRows());
  }

  Iterator IterateRows() { return Iterator(this, Table::IterateRows()); }

  ConstIterator FilterToIterator(const Query& q) const {
    return ConstIterator(this, QueryToIterator(q));
  }

  Iterator FilterToIterator(const Query& q) {
    return Iterator(this, QueryToIterator(q));
  }

  void ShrinkToFit() {
    type_.ShrinkToFit();
    ts_.ShrinkToFit();
    utid_.ShrinkToFit();
    exception_level_.ShrinkToFit();
    instruction_frame_id_.ShrinkToFit();
    operation_.ShrinkToFit();
    data_virtual_address_.ShrinkToFit();
    data_physical_address_.ShrinkToFit();
    total_latency_.ShrinkToFit();
    issue_latency_.ShrinkToFit();
    translation_latency_.ShrinkToFit();
    events_bitmask_.ShrinkToFit();
    data_source_.ShrinkToFit();
  }

  ConstRowReference operator[](uint32_t r) const {
    return ConstRowReference(this, r);
  }
  RowReference operator[](uint32_t r) { return RowReference(this, r); }
  ConstRowReference operator[](RowNumber r) const {
    return ConstRowReference(this, r.row_number());
  }
  RowReference operator[](RowNumber r) {
    return RowReference(this, r.row_number());
  }

  std::optional<ConstRowReference> FindById(Id find_id) const {
    std::optional<uint32_t> row = id().IndexOf(find_id);
    return row ? std::make_optional(ConstRowReference(this, *row))
               : std::nullopt;
  }

  std::optional<RowReference> FindById(Id find_id) {
    std::optional<uint32_t> row = id().IndexOf(find_id);
    return row ? std::make_optional(RowReference(this, *row)) : std::nullopt;
  }

  IdAndRow Insert(const Row& row) {
    uint32_t row_number = row_count();
    Id id = Id{row_number};
    type_.Append(string_pool()->InternString(row.type()));
    mutable_ts()->Append(row.ts);
    mutable_utid()->Append(row.utid);
    mutable_exception_level()->Append(row.exception_level);
    mutable_instruction_frame_id()->Append(row.instruction_frame_id);
    mutable_operation()->Append(row.operation);
    mutable_data_virtual_address()->Append(row.data_virtual_address);
    mutable_data_physical_address()->Append(row.data_physical_address);
    mutable_total_latency()->Append(row.total_latency);
    mutable_issue_latency()->Append(row.issue_latency);
    mutable_translation_latency()->Append(row.translation_latency);
    mutable_events_bitmask()->Append(row.events_bitmask);
    mutable_data_source()->Append(row.data_source);
    UpdateSelfOverlayAfterInsert();
    return IdAndRow{id, row_number, RowReference(this, row_number),
                     RowNumber(row_number)};
  }

  

  const IdColumn<SpeRecordTable::Id>& id() const {
    return static_cast<const ColumnType::id&>(columns()[ColumnIndex::id]);
  }
  const TypedColumn<StringPool::Id>& type() const {
    return static_cast<const ColumnType::type&>(columns()[ColumnIndex::type]);
  }
  const TypedColumn<int64_t>& ts() const {
    return static_cast<const ColumnType::ts&>(columns()[ColumnIndex::ts]);
  }
  const TypedColumn<std::optional<uint32_t>>& utid() const {
    return static_cast<const ColumnType::utid&>(columns()[ColumnIndex::utid]);
  }
  const TypedColumn<StringPool::Id>& exception_level() const {
    return static_cast<const ColumnType::exception_level&>(columns()[ColumnIndex::exception_level]);
  }
  const TypedColumn<std::optional<StackProfileFrameTable::Id>>& instruction_frame_id() const {
    return static_cast<const ColumnType::instruction_frame_id&>(columns()[ColumnIndex::instruction_frame_id]);
  }
  const TypedColumn<StringPool::Id>& operation() const {
    return static_cast<const ColumnType::operation&>(columns()[ColumnIndex::operation]);
  }
  const TypedColumn<int64_t>& data_virtual_address() const {
    return static_cast<const ColumnType::data_virtual_address&>(columns()[ColumnIndex::data_virtual_address]);
  }
  const TypedColumn<int64_t>& data_physical_address() const {
    return static_cast<const ColumnType::data_physical_address&>(columns()[ColumnIndex::data_physical_address]);
  }
  const TypedColumn<uint32_t>& total_latency() const {
    return static_cast<const ColumnType::total_latency&>(columns()[ColumnIndex::total_latency]);
  }
  const TypedColumn<uint32_t>& issue_latency() const {
    return static_cast<const ColumnType::issue_latency&>(columns()[ColumnIndex::issue_latency]);
  }
  const TypedColumn<uint32_t>& translation_latency() const {
    return static_cast<const ColumnType::translation_latency&>(columns()[ColumnIndex::translation_latency]);
  }
  const TypedColumn<int64_t>& events_bitmask() const {
    return static_cast<const ColumnType::events_bitmask&>(columns()[ColumnIndex::events_bitmask]);
  }
  const TypedColumn<StringPool::Id>& data_source() const {
    return static_cast<const ColumnType::data_source&>(columns()[ColumnIndex::data_source]);
  }

  TypedColumn<int64_t>* mutable_ts() {
    return static_cast<ColumnType::ts*>(
        GetColumn(ColumnIndex::ts));
  }
  TypedColumn<std::optional<uint32_t>>* mutable_utid() {
    return static_cast<ColumnType::utid*>(
        GetColumn(ColumnIndex::utid));
  }
  TypedColumn<StringPool::Id>* mutable_exception_level() {
    return static_cast<ColumnType::exception_level*>(
        GetColumn(ColumnIndex::exception_level));
  }
  TypedColumn<std::optional<StackProfileFrameTable::Id>>* mutable_instruction_frame_id() {
    return static_cast<ColumnType::instruction_frame_id*>(
        GetColumn(ColumnIndex::instruction_frame_id));
  }
  TypedColumn<StringPool::Id>* mutable_operation() {
    return static_cast<ColumnType::operation*>(
        GetColumn(ColumnIndex::operation));
  }
  TypedColumn<int64_t>* mutable_data_virtual_address() {
    return static_cast<ColumnType::data_virtual_address*>(
        GetColumn(ColumnIndex::data_virtual_address));
  }
  TypedColumn<int64_t>* mutable_data_physical_address() {
    return static_cast<ColumnType::data_physical_address*>(
        GetColumn(ColumnIndex::data_physical_address));
  }
  TypedColumn<uint32_t>* mutable_total_latency() {
    return static_cast<ColumnType::total_latency*>(
        GetColumn(ColumnIndex::total_latency));
  }
  TypedColumn<uint32_t>* mutable_issue_latency() {
    return static_cast<ColumnType::issue_latency*>(
        GetColumn(ColumnIndex::issue_latency));
  }
  TypedColumn<uint32_t>* mutable_translation_latency() {
    return static_cast<ColumnType::translation_latency*>(
        GetColumn(ColumnIndex::translation_latency));
  }
  TypedColumn<int64_t>* mutable_events_bitmask() {
    return static_cast<ColumnType::events_bitmask*>(
        GetColumn(ColumnIndex::events_bitmask));
  }
  TypedColumn<StringPool::Id>* mutable_data_source() {
    return static_cast<ColumnType::data_source*>(
        GetColumn(ColumnIndex::data_source));
  }

 private:
  
  
  ColumnStorage<ColumnType::ts::stored_type> ts_;
  ColumnStorage<ColumnType::utid::stored_type> utid_;
  ColumnStorage<ColumnType::exception_level::stored_type> exception_level_;
  ColumnStorage<ColumnType::instruction_frame_id::stored_type> instruction_frame_id_;
  ColumnStorage<ColumnType::operation::stored_type> operation_;
  ColumnStorage<ColumnType::data_virtual_address::stored_type> data_virtual_address_;
  ColumnStorage<ColumnType::data_physical_address::stored_type> data_physical_address_;
  ColumnStorage<ColumnType::total_latency::stored_type> total_latency_;
  ColumnStorage<ColumnType::issue_latency::stored_type> issue_latency_;
  ColumnStorage<ColumnType::translation_latency::stored_type> translation_latency_;
  ColumnStorage<ColumnType::events_bitmask::stored_type> events_bitmask_;
  ColumnStorage<ColumnType::data_source::stored_type> data_source_;

  RefPtr<column::StorageLayer> id_storage_layer_;
  RefPtr<column::StorageLayer> type_storage_layer_;
  RefPtr<column::StorageLayer> ts_storage_layer_;
  RefPtr<column::StorageLayer> utid_storage_layer_;
  RefPtr<column::StorageLayer> exception_level_storage_layer_;
  RefPtr<column::StorageLayer> instruction_frame_id_storage_layer_;
  RefPtr<column::StorageLayer> operation_storage_layer_;
  RefPtr<column::StorageLayer> data_virtual_address_storage_layer_;
  RefPtr<column::StorageLayer> data_physical_address_storage_layer_;
  RefPtr<column::StorageLayer> total_latency_storage_layer_;
  RefPtr<column::StorageLayer> issue_latency_storage_layer_;
  RefPtr<column::StorageLayer> translation_latency_storage_layer_;
  RefPtr<column::StorageLayer> events_bitmask_storage_layer_;
  RefPtr<column::StorageLayer> data_source_storage_layer_;

  RefPtr<column::OverlayLayer> utid_null_layer_;
  RefPtr<column::OverlayLayer> instruction_frame_id_null_layer_;
};

}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_TABLES_PERF_TABLES_PY_H_
