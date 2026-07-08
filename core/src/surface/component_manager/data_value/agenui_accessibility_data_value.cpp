#include "agenui_accessibility_data_value.h"
#include "surface/datamodel/agenui_idata_model.h"
#include "surface/datamodel/agenui_data_observer.h"
#include "surface/virtual_dom/agenui_component_snapshot.h"
#include "surface/agenui_serializable_data_impl.h"

namespace agenui {

AccessibilityDataValue::AccessibilityDataValue(IDataValueContext* context) : DataValue(context) {
}

AccessibilityDataValue::AccessibilityDataValue(IDataValueContext* context, const std::map<std::string, std::shared_ptr<DataValue>>& fields) : DataValue(context), _fields(fields) {
}

AccessibilityDataValue::~AccessibilityDataValue() {
    unbind();
}

DataType AccessibilityDataValue::getDataType() const {
    return DataType::AccessibilityData;
}

DataBindingStatus AccessibilityDataValue::getDataBindingStatus() const {
    std::vector<DataBindingStatus> statuses;
    for (const auto& pair : _fields) {
        if (pair.second) {
            statuses.emplace_back(pair.second->getDataBindingStatus());
        }
    }
    return aggregateBindingStatus(statuses);
}

SerializableData AccessibilityDataValue::getValueData() const {
    auto impl = SerializableData::Impl::createObject();

    for (const auto& pair : _fields) {
        if (pair.second) {
            auto valueData = pair.second->getValueData();
            if (valueData.isValid()) {
                impl->set(pair.first, valueData);
            }
        }
    }

    return SerializableData(impl);
}

void AccessibilityDataValue::setField(const std::string& fieldName, std::shared_ptr<DataValue> value) {
    _fields[fieldName] = value;
}

std::shared_ptr<DataValue> AccessibilityDataValue::getField(const std::string& fieldName) const {
    auto it = _fields.find(fieldName);
    if (it != _fields.end()) {
        return it->second;
    }
    return nullptr;
}

const std::map<std::string, std::shared_ptr<DataValue>>& AccessibilityDataValue::getAllFields() const {
    return _fields;
}

void AccessibilityDataValue::bind(IDataChangedObserver* observer) {
    for (auto& pair : _fields) {
        if (pair.second) {
            pair.second->bind(observer);
        }
    }
}

void AccessibilityDataValue::unbind() {
    for (auto& pair : _fields) {
        if (pair.second) {
            pair.second->unbind();
        }
    }
}

std::shared_ptr<DataValue> AccessibilityDataValue::cloneAsTemplate(IDataValueContext* context, const std::string& rootDataPath) const {
    std::map<std::string, std::shared_ptr<DataValue>> clonedFields;

    for (const auto& pair : _fields) {
        if (pair.second) {
            clonedFields[pair.first] = pair.second->cloneAsTemplate(context, rootDataPath);
        }
    }

    return std::make_shared<AccessibilityDataValue>(context, clonedFields);
}

}  // namespace agenui
