#pragma once

#include "agenui_data_value_base.h"
#include <map>
#include <memory>
#include <string>

namespace agenui {

class IDataModel;
class IDataChangedObserver;

/**
 * @brief Accessibility data value
 * @remark Represents a collection of accessibility properties (label, description, role, etc.),
 *         supporting dynamic data binding per field.
 */
class AccessibilityDataValue : public DataValue {
public:
    explicit AccessibilityDataValue(IDataValueContext* context);
    AccessibilityDataValue(IDataValueContext* context, const std::map<std::string, std::shared_ptr<DataValue>>& fields);
    ~AccessibilityDataValue() override;

    DataType getDataType() const override;
    DataBindingStatus getDataBindingStatus() const override;
    SerializableData getValueData() const override;
    void bind(IDataChangedObserver* observer) override;
    void unbind() override;
    std::shared_ptr<DataValue> cloneAsTemplate(IDataValueContext* context, const std::string& rootDataPath) const override;

    void setField(const std::string& fieldName, std::shared_ptr<DataValue> value);
    std::shared_ptr<DataValue> getField(const std::string& fieldName) const;
    const std::map<std::string, std::shared_ptr<DataValue>>& getAllFields() const;

private:
    std::map<std::string, std::shared_ptr<DataValue>> _fields;
};

}  // namespace agenui
