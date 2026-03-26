#include "core/mock/MockRegistryManager.h"

namespace PJ {

MockRegistryManager::MockRegistryManager(QObject* parent) : RegistryManager(parent) {
  {
    Extension e;
    e.id = "csv-loader"; e.name = "CSV Loader";
    e.description = "Load CSV/TSV files with automatic column detection and configurable delimiters.";
    e.author = "PlotJuggler Team"; e.publisher = "PlotJuggler";
    e.license = "MIT"; e.website = "https://github.com/facontidavide/PlotJuggler";
    e.category = "data_loader"; e.tags = {"csv", "tsv", "file"};
    e.version = "1.0.0"; e.min_plotjuggler_version = "3.8.0";
    e.changelog["1.0.0"] = "Initial release with CSV and TSV support.";
    mock_extensions_.append(e);
  }
  {
    Extension e;
    e.id = "ros2-streaming"; e.name = "ROS 2 Streaming";
    e.description = "Stream topics live from a ROS 2 network via DDS. Supports QoS configuration.";
    e.author = "ROS Community"; e.publisher = "ros-community";
    e.license = "Apache-2.0"; e.website = "https://github.com/ros-community/plotjuggler-ros2";
    e.category = "data_streamer"; e.tags = {"ros2", "dds", "live", "robotics"};
    e.version = "1.2.0"; e.min_plotjuggler_version = "3.8.0";
    e.changelog["1.2.0"] = "Added QoS profile selector and topic type filter.";
    e.changelog["1.1.0"] = "Initial public release.";
    mock_extensions_.append(e);
  }
  {
    Extension e;
    e.id = "mcap-loader"; e.name = "MCAP Loader";
    e.description = "Load MCAP log files (Foxglove format). Supports multi-channel time-indexed data.";
    e.author = "Foxglove Technologies"; e.publisher = "foxglove";
    e.license = "MIT"; e.website = "https://foxglove.dev";
    e.category = "data_loader"; e.tags = {"mcap", "foxglove", "log"};
    e.version = "2.1.0"; e.min_plotjuggler_version = "3.9.0";
    e.changelog["2.1.0"] = "Performance improvements for large MCAP files.";
    e.changelog["2.0.0"] = "Full MCAP spec v2 compliance.";
    mock_extensions_.append(e);
  }
  {
    Extension e;
    e.id = "fft-toolbox"; e.name = "FFT Toolbox";
    e.description = "Frequency analysis tools: FFT, spectrogram, windowing functions (Hann, Blackman).";
    e.author = "Signal Processing Labs"; e.publisher = "spl";
    e.license = "GPL-3.0"; e.website = "https://github.com/spl/plotjuggler-fft";
    e.category = "toolbox"; e.tags = {"fft", "frequency", "spectrum", "signal"};
    e.version = "0.9.2"; e.min_plotjuggler_version = "3.8.0";
    e.changelog["0.9.2"] = "Added Blackman-Harris window.";
    e.changelog["0.9.0"] = "Beta release.";
    mock_extensions_.append(e);
  }
  {
    Extension e;
    e.id = "can-parser"; e.name = "CAN Bus Parser";
    e.description = "Parse CAN bus messages using DBC database files. Decodes signals automatically.";
    e.author = "Automotive Tools Group"; e.publisher = "atg";
    e.license = "MIT"; e.website = "https://github.com/atg/plotjuggler-can";
    e.category = "parser"; e.tags = {"can", "dbc", "automotive", "bus"};
    e.version = "1.3.1"; e.min_plotjuggler_version = "3.8.0";
    e.changelog["1.3.1"] = "Fixed signed integer decoding for CAN signals.";
    e.changelog["1.3.0"] = "DBC multiplexed messages support.";
    mock_extensions_.append(e);
  }
  {
    Extension e;
    e.id = "ros-bundle"; e.name = "ROS Bundle";
    e.description = "All-in-one bundle: ROS 1 bag loader, ROS 2 streaming, and rosout log viewer.";
    e.author = "PlotJuggler Team"; e.publisher = "PlotJuggler";
    e.license = "LGPL-2.1"; e.website = "https://github.com/facontidavide/PlotJuggler";
    e.category = "bundle"; e.tags = {"ros", "ros2", "bag", "bundle"};
    e.version = "3.0.0"; e.min_plotjuggler_version = "3.9.0";
    e.changelog["3.0.0"] = "Unified ROS 1+2 bundle. Requires PlotJuggler 3.9+.";
    mock_extensions_.append(e);
  }
}

void MockRegistryManager::fetchRegistry(const QUrl& /*url*/) {
  // Data is already in mock_extensions_; emit synchronously so the caller
  // sees a populated catalog before returning from this call.
  emit fetchStarted();
  emit fetchFinished(true);
}

QList<Extension> MockRegistryManager::extensions() const {
  return mock_extensions_;
}

Extension MockRegistryManager::findById(const QString& id) const {
  for (const auto& ext : mock_extensions_)
    if (ext.id == id) return ext;
  return {};
}

}  // namespace PJ
