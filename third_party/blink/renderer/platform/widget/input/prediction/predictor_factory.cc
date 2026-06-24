// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/widget/input/prediction/predictor_factory.h"

#include "third_party/blink/public/common/features.h"
#include "ui/base/prediction/empty_predictor.h"
#include "ui/base/ui_base_features.h"

namespace blink {

namespace {
using input_prediction::PredictorType;
}

// Set to UINT_MAX to trigger querying feature flags.
unsigned int PredictorFactory::predictor_options_ = UINT_MAX;

PredictorType PredictorFactory::GetPredictorTypeFromName(const std::string& predictor_name)
{
    if (predictor_name == ::features::kPredictorNameLinearResampling)
        return PredictorType::kScrollPredictorTypeLinearResampling;
    else if (predictor_name == ::features::kPredictorNameLsq)
        return PredictorType::kScrollPredictorTypeLsq;
    else if (predictor_name == ::features::kPredictorNameKalman)
        return PredictorType::kScrollPredictorTypeKalman;
    else if (predictor_name == ::features::kPredictorNameLinearFirst)
        return PredictorType::kScrollPredictorTypeLinearFirst;
    else if (predictor_name == ::features::kPredictorNameLinearSecond)
        return PredictorType::kScrollPredictorTypeLinearSecond;
    else
        return PredictorType::kScrollPredictorTypeEmpty;
}

std::unique_ptr<ui::InputPredictor> PredictorFactory::GetPredictor(PredictorType predictor_type)
{
    (void)predictor_type;
    return std::make_unique<ui::EmptyPredictor>();
}

unsigned int PredictorFactory::GetKalmanPredictorOptions()
{
    if (predictor_options_ == UINT_MAX)
        predictor_options_ = 0;
    return predictor_options_;
}

} // namespace blink
