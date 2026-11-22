from setuptools import setup

package_name = "luckfox_learning"

setup(
    name=package_name,
    version="0.0.0",
    packages=[package_name],
    # 空标记文件供 ament 索引定位包，package.xml 提供包的元数据。
    data_files=[
        ("share/ament_index/resource_index/packages", ["resource/" + package_name]),
        ("share/" + package_name, ["package.xml"]),
    ],
    install_requires=["setuptools"],
    maintainer="name718",
    maintainer_email="1547996098@qq.com",
    description="Luckfox 上的 ROS 2 Python 学习包。",
    license="TODO",
    # 安装后，ros2 run luckfox_learning learning 会调用下面的 Python 入口。
    entry_points={"console_scripts": ["learning = luckfox_learning.main:main"]},
)
