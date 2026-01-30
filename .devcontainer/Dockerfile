FROM mcr.microsoft.com/devcontainers/base:debian

ARG USERNAME=dev
ARG USER_UID=1000
ARG USER_GID=$USER_UID
ARG IDF_VERSION=v5.4

ENV IDF_PATH=/opt/esp-idf
ENV IDF_TOOLS_PATH=/opt/esp-idf-tools

# Remove unused zsh/oh-my-zsh to save space
RUN rm -rf /home/*/.oh-my-zsh /root/.oh-my-zsh && \
    apt-get purge -y zsh && apt-get autoremove -y

# Rename existing vscode user to dev (base image has vscode with UID 1000)
RUN usermod -l $USERNAME -d /home/$USERNAME -m vscode && \
    groupmod -n $USERNAME vscode && \
    echo $USERNAME ALL=\(root\) NOPASSWD:ALL > /etc/sudoers.d/$USERNAME && \
    chmod 0440 /etc/sudoers.d/$USERNAME

# Install build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    git \
    wget \
    curl \
    flex \
    bison \
    gperf \
    cmake \
    ninja-build \
    ccache \
    libffi-dev \
    libssl-dev \
    dfu-util \
    libusb-1.0-0 \
    python3 \
    python3-pip \
    python3-venv \
    udev \
    usbutils \
    minicom \
    screen \
    && rm -rf /var/lib/apt/lists/*

# Install Node.js LTS via NodeSource
RUN curl -fsSL https://deb.nodesource.com/setup_lts.x | bash - && \
    apt-get install -y nodejs && \
    rm -rf /var/lib/apt/lists/*

# Setup udev rules for ESP32 USB adapters
RUN echo 'SUBSYSTEM=="tty", ATTRS{idVendor}=="303a", MODE="0666"' > /etc/udev/rules.d/99-esp32.rules && \
    echo 'SUBSYSTEM=="tty", ATTRS{idVendor}=="10c4", MODE="0666"' >> /etc/udev/rules.d/99-esp32.rules && \
    echo 'SUBSYSTEM=="tty", ATTRS{idVendor}=="1a86", MODE="0666"' >> /etc/udev/rules.d/99-esp32.rules && \
    echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="303a", MODE="0666"' >> /etc/udev/rules.d/99-esp32.rules && \
    echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="10c4", MODE="0666"' >> /etc/udev/rules.d/99-esp32.rules && \
    echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="1a86", MODE="0666"' >> /etc/udev/rules.d/99-esp32.rules

# Clone ESP-IDF
RUN git clone -b ${IDF_VERSION} --depth 1 --recursive --shallow-submodules \
    https://github.com/espressif/esp-idf.git ${IDF_PATH}

# Install ESP-IDF tools
RUN ${IDF_PATH}/install.sh all && \
    chown -R ${USER_UID}:${USER_GID} ${IDF_PATH} ${IDF_TOOLS_PATH}

# Add ESP-IDF to bashrc for dev user
RUN echo '. ${IDF_PATH}/export.sh' >> /home/${USERNAME}/.bashrc && \
    chown ${USER_UID}:${USER_GID} /home/${USERNAME}/.bashrc

# Create .ssh directory for dev user
RUN mkdir -p /home/${USERNAME}/.ssh && \
    chown -R ${USER_UID}:${USER_GID} /home/${USERNAME}/.ssh && \
    chmod 700 /home/${USERNAME}/.ssh

USER $USERNAME
WORKDIR /workspaces
